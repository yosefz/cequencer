#!/usr/bin/env python3
"""
IDL Code Generator for Chronicle Messages

Scans for .idl files and generates C++ zero-copy encoder/decoder classes.
- Each .idl file becomes a namespace based on its path
- Each enum/object gets its own .hpp file
- Each message gets Encoder + Decoder classes (zero-copy, wrap ZBuffer)
- Generated handlers use Decoder references (no allocations)

Usage:
    python codegen.py                    # Scan current dir for .idl files
    python codegen.py -o ../libs/msgs/   # Specify output directory

Directory Structure:
    idl/
    ├── trading/
    │   ├── orders.idl      -> messages::trading::orders
    │   └── market.idl      -> messages::trading::market
    └── common.idl          -> messages::common

    Output:
    libs/messages/
    ├── include/
    │   ├── schema/
    │   │   ├── enums/
    │   │   │   └── Side.hpp
    │   │   ├── commands/
    │   │   │   └── NewOrderSingleCommand.hpp  (Encoder + Decoder)
    │   │   ├── events/
    │   │   │   └── ExecutionReportEvent.hpp   (Encoder + Decoder)
    │   │   ├── handlers/
    │   │   │   ├── CommandHandler.hpp
    │   │   │   ├── EventHandler.hpp
    │   │   │   ├── EventWriter.hpp
    │   │   │   ├── Dispatcher.hpp
    │   │   │   └── StreamDumper.hpp
    │   │   └── all.hpp
    │   └── common/
    │       └── List.hpp
    └── CMakeLists.txt
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


# =============================================================================
# Type System
# =============================================================================

PRIMITIVE_TYPES = {
    'int8':    ('std::int8_t',   1),
    'int16':   ('std::int16_t',  2),
    'int32':   ('std::int32_t',  4),
    'int64':   ('std::int64_t',  8),
    'uint8':   ('std::uint8_t',  1),
    'uint16':  ('std::uint16_t', 2),
    'uint32':  ('std::uint32_t', 4),
    'uint64':  ('std::uint64_t', 8),
    'float32': ('float',         4),
    'float64': ('double',        8),
}

# Base fields for all messages (Commands and Events)
# messageVersion is first so decoder can check version before reading rest
BASE_MESSAGE_FIELDS = [
    ('uint16', 'messageVersion'),   # Version of this message format
    ('uint64', 'sequenceNumber'),
    ('string(32)', 'msgSender'),
    ('uint64', 'sendingTimestamp'),
]

# Size of base fields: 2 + 8 + 32 + 8 = 50 bytes
BASE_FIELDS_SIZE = 2 + 8 + 32 + 8


@dataclass
class Field:
    type_str: str
    name: str
    cpp_type: str = ''
    size: int = 0
    is_primitive: bool = False
    is_string: bool = False
    is_enum: bool = False
    is_object: bool = False
    is_list: bool = False
    string_len: int = 0
    list_element_type: str = ''
    list_capacity: int = 0
    since_version: int = 1  # Version when this field was added


@dataclass
class EnumValue:
    name: str
    since_version: int = 1  # Version when this value was added


@dataclass
class Enum:
    name: str
    values: list[EnumValue] = field(default_factory=list)
    since_version: int = 1  # Version when this enum was added


@dataclass
class Object:
    name: str
    fields: list[Field] = field(default_factory=list)
    size: int = 0
    since_version: int = 1  # Version when this object was added


@dataclass
class Message:
    kind: str
    name: str
    fields: list[Field] = field(default_factory=list)
    size: int = 0
    type_id: int | None = None  # User-defined TYPE_ID (required)
    inline: bool = True
    since_version: int = 1  # Version when this message was added
    no_dump: bool = False  # If true, serialize() outputs nothing

    @property
    def class_name(self) -> str:
        """Name with Command/Event suffix."""
        suffix = 'Command' if self.kind == 'command' else 'Event'
        return f"{self.name}{suffix}"

    @property
    def max_field_version(self) -> int:
        """Highest version among all fields."""
        if not self.fields:
            return self.since_version
        return max(f.since_version for f in self.fields)


@dataclass
class Schema:
    name: str  # namespace name (from filename)
    path: Path  # relative path from idl root
    version: int = 1  # Schema version
    enums: dict[str, Enum] = field(default_factory=dict)
    objects: dict[str, Object] = field(default_factory=dict)
    messages: dict[str, Message] = field(default_factory=dict)

    @property
    def namespace(self) -> str:
        """Full namespace path like 'trading::orders'"""
        parts = list(self.path.parts[:-1]) + [self.path.stem]
        return '::'.join(parts)

    @property
    def namespace_parts(self) -> list[str]:
        return list(self.path.parts[:-1]) + [self.path.stem]


# =============================================================================
# Parser
# =============================================================================

def parse_idl(filepath: Path, relative_path: Path) -> Schema:
    schema = Schema(name=filepath.stem, path=relative_path)

    with open(filepath, 'r') as f:
        lines = f.readlines()

    current_block = None
    current_item = None
    pending_since_version = None  # Holds @sinceVersion until next item
    pending_type_id = None        # Holds @typeId until next message
    pending_no_dump = False       # Holds @noDump until next message

    for line_num, line in enumerate(lines, 1):
        # Strip comments
        if '#' in line:
            line = line[:line.index('#')]
        line = line.rstrip()

        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip())
        content = line.strip()

        # Check for @sinceVersion decorator
        version_match = re.match(r'@sinceVersion\((\d+)\)', content)
        if version_match:
            pending_since_version = int(version_match.group(1))
            continue

        # Check for @typeId decorator
        typeid_match = re.match(r'@typeId\((\d+)\)', content)
        if typeid_match:
            pending_type_id = int(typeid_match.group(1))
            continue

        # Check for @noDump decorator
        if content == '@noDump':
            pending_no_dump = True
            continue

        if indent == 0:
            parts = content.split()

            # Handle 'version N' directive
            if parts[0] == 'version':
                if len(parts) != 2:
                    raise SyntaxError(f"{filepath}:{line_num}: Expected 'version N'")
                schema.version = int(parts[1])
                continue

            if len(parts) < 2:
                raise SyntaxError(f"{filepath}:{line_num}: Expected 'type Name', got '{content}'")

            block_type, name = parts[0], parts[1]
            flags = parts[2:] if len(parts) > 2 else []

            # Get since_version from pending decorator or default to 1
            since_version = pending_since_version if pending_since_version else 1
            pending_since_version = None

            is_inline = True
            for flag in flags:
                if flag in ('[outline]', '[cold]'):
                    is_inline = False
                elif flag in ('[inline]', '[hot]'):
                    is_inline = True

            if block_type == 'enum':
                current_block = 'enum'
                current_item = Enum(name=name, since_version=since_version)
                schema.enums[name] = current_item
                pending_no_dump = False  # Reset (not applicable to enums)
            elif block_type == 'object':
                current_block = 'object'
                current_item = Object(name=name, since_version=since_version)
                schema.objects[name] = current_item
                pending_no_dump = False  # Reset (not applicable to objects)
            elif block_type in ('message', 'command', 'event'):
                if pending_type_id is None:
                    raise SyntaxError(f"{filepath}:{line_num}: Message '{name}' requires @typeId(N) decorator")

                current_block = 'message'
                current_item = Message(
                    kind=block_type if block_type != 'message' else 'event',
                    name=name,
                    inline=is_inline,
                    since_version=since_version,
                    type_id=pending_type_id,
                    no_dump=pending_no_dump
                )
                schema.messages[name] = current_item
                pending_type_id = None
                pending_no_dump = False
            else:
                raise SyntaxError(f"{filepath}:{line_num}: Unknown block type '{block_type}'")

        elif indent > 0 and current_item:
            # Get since_version from pending decorator or default to 1
            since_version = pending_since_version if pending_since_version else 1
            pending_since_version = None

            if current_block == 'enum':
                current_item.values.append(EnumValue(name=content, since_version=since_version))
            else:
                # Parse field: "type name" - handling nested parentheses
                paren_depth = 0
                last_space = -1
                for i, ch in enumerate(content):
                    if ch == '(':
                        paren_depth += 1
                    elif ch == ')':
                        paren_depth -= 1
                    elif ch == ' ' and paren_depth == 0:
                        last_space = i

                if last_space == -1:
                    raise SyntaxError(f"{filepath}:{line_num}: Expected 'type name', got '{content}'")

                field_type = content[:last_space]
                field_name = content[last_space + 1:]
                current_item.fields.append(Field(
                    type_str=field_type,
                    name=field_name,
                    since_version=since_version
                ))

    return schema


def scan_idl_files(root_dir: Path) -> list[Schema]:
    """Scan for all .idl files and parse them."""
    schemas = []

    for idl_file in sorted(root_dir.rglob('*.idl')):
        relative = idl_file.relative_to(root_dir)
        print(f"  Found: {relative}")
        schema = parse_idl(idl_file, relative)
        schemas.append(schema)

    return schemas


# =============================================================================
# Type Resolution
# =============================================================================

def resolve_field_type(f: Field, schema: Schema, all_schemas: list[Schema]) -> None:
    type_str = f.type_str

    if type_str in PRIMITIVE_TYPES:
        f.is_primitive = True
        f.cpp_type, f.size = PRIMITIVE_TYPES[type_str]
        return

    match = re.match(r'string\((\d+)\)', type_str)
    if match:
        f.is_string = True
        f.string_len = int(match.group(1))
        f.size = f.string_len
        f.cpp_type = f'std::array<char, {f.string_len}>'
        return

    match = re.match(r'list\((\w+),\s*(\d+)\)', type_str)
    if match:
        f.is_list = True
        f.list_element_type = match.group(1)
        f.list_capacity = int(match.group(2))

        elem_size = get_type_size(f.list_element_type, schema, all_schemas)
        f.size = 4 + f.list_capacity * elem_size
        f.cpp_type = f'List<{get_cpp_type(f.list_element_type, schema, all_schemas)}, {f.list_capacity}>'
        return

    if type_str in schema.enums:
        f.is_enum = True
        f.cpp_type = type_str
        f.size = 4
        return

    if type_str in schema.objects:
        f.is_object = True
        f.cpp_type = type_str
        resolve_object(schema.objects[type_str], schema, all_schemas)
        f.size = schema.objects[type_str].size
        return

    raise TypeError(f"Unknown type: {type_str}")


def get_type_size(type_str: str, schema: Schema, all_schemas: list[Schema]) -> int:
    if type_str in PRIMITIVE_TYPES:
        return PRIMITIVE_TYPES[type_str][1]
    if type_str in schema.enums:
        return 4
    if type_str in schema.objects:
        resolve_object(schema.objects[type_str], schema, all_schemas)
        return schema.objects[type_str].size
    raise TypeError(f"Unknown type: {type_str}")


def get_cpp_type(type_str: str, schema: Schema, all_schemas: list[Schema]) -> str:
    if type_str in PRIMITIVE_TYPES:
        return PRIMITIVE_TYPES[type_str][0]
    if type_str in schema.enums:
        return type_str
    if type_str in schema.objects:
        return type_str
    raise TypeError(f"Unknown type: {type_str}")


def resolve_object(obj: Object, schema: Schema, all_schemas: list[Schema]) -> None:
    if obj.size > 0:
        return

    total_size = 0
    for f in obj.fields:
        resolve_field_type(f, schema, all_schemas)
        total_size += f.size
    obj.size = total_size


def resolve_message(msg: Message, schema: Schema, all_schemas: list[Schema]) -> None:
    base_fields = []
    for type_str, name in BASE_MESSAGE_FIELDS:
        f = Field(type_str=type_str, name=name)
        resolve_field_type(f, schema, all_schemas)
        base_fields.append(f)

    msg.fields = base_fields + msg.fields

    total_size = sum(f.size for f in base_fields)
    for f in msg.fields[len(base_fields):]:
        resolve_field_type(f, schema, all_schemas)
        total_size += f.size

    msg.size = total_size


def resolve_schema(schema: Schema, all_schemas: list[Schema]) -> None:
    """Resolve all types in schema."""
    for obj in schema.objects.values():
        resolve_object(obj, schema, all_schemas)

    # Check for duplicate type_ids
    type_ids = {}
    for msg in schema.messages.values():
        if msg.type_id in type_ids:
            raise ValueError(f"Duplicate @typeId({msg.type_id}): {msg.name} and {type_ids[msg.type_id]}")
        type_ids[msg.type_id] = msg.name
        resolve_message(msg, schema, all_schemas)


# =============================================================================
# Code Generation - Individual Files
# =============================================================================

def header_guard(category: str, name: str) -> tuple[str, str]:
    guard = f'MESSAGES_{category.upper()}_{name.upper()}_HPP'
    return f"#ifndef {guard}\n#define {guard}\n", f"#endif // {guard}\n"


def namespace_open(schema_ns: list[str], category: str) -> str:
    if schema_ns:
        return f"namespace messages::{('::'.join(schema_ns))}::{category} {{"
    return f"namespace messages::{category} {{"


def namespace_close(schema_ns: list[str], category: str) -> str:
    if schema_ns:
        return f"}} // namespace messages::{('::'.join(schema_ns))}::{category}"
    return f"}} // namespace messages::{category}"


def full_namespace(schema_ns: list[str], category: str) -> str:
    if schema_ns:
        return f"messages::{('::'.join(schema_ns))}::{category}"
    return f"messages::{category}"


def generate_enum_file(enum: Enum, schema: Schema) -> str:
    guard_begin, guard_end = header_guard('enums', enum.name)
    schema_ns = schema.namespace_parts

    lines = [
        guard_begin,
        "#include <cstdint>",
        "",
        namespace_open(schema_ns, 'enums'),
        "",
        f"enum class {enum.name} : std::int32_t {{",
    ]

    for i, val in enumerate(enum.values):
        lines.append(f"    {val.name} = {i},")

    lines.extend([
        "};",
        "",
        namespace_close(schema_ns, 'enums'),
        "",
        guard_end,
    ])

    return '\n'.join(lines)


def generate_object_file(obj: Object, schema: Schema) -> str:
    guard_begin, guard_end = header_guard('objects', obj.name)
    schema_ns = schema.namespace_parts

    # Collect dependencies
    enum_deps = set()
    obj_deps = set()
    for f in obj.fields:
        if f.is_enum:
            enum_deps.add(f.cpp_type)
        elif f.is_object:
            obj_deps.add(f.cpp_type)
        elif f.is_list and f.list_element_type in schema.objects:
            obj_deps.add(f.list_element_type)

    needs_list = any(f.is_list for f in obj.fields)

    lines = [
        guard_begin,
        "#include <array>",
        "#include <cstdint>",
        "#include <cstring>",
    ]

    if needs_list:
        lines.append('#include <common/List.hpp>')

    lines.append("")

    # Include enum dependencies
    for dep in sorted(enum_deps):
        lines.append(f'#include "../enums/{dep}.hpp"')

    # Include object dependencies
    for dep in sorted(obj_deps):
        lines.append(f'#include "{dep}.hpp"')

    if enum_deps or obj_deps:
        lines.append("")

    lines.extend([
        namespace_open(schema_ns, 'objects'),
        "",
        f"struct {obj.name} {{",
        f"    static constexpr std::size_t SIZE = {obj.size};",
        "",
    ])

    for f in obj.fields:
        cpp_type = qualify_type(f, schema)
        lines.append(f"    {cpp_type} {f.name}{{}};")

    lines.append("")
    lines.append("    void encode(std::byte* buf) const noexcept {")
    offset = 0
    for f in obj.fields:
        lines.extend(generate_field_encode(f, offset))
        offset += f.size
    lines.append("    }")
    lines.append("")

    lines.append(f"    static {obj.name} decode(const std::byte* buf) noexcept {{")
    lines.append(f"        {obj.name} obj;")
    offset = 0
    for f in obj.fields:
        lines.extend(generate_field_decode(f, "obj.", offset))
        offset += f.size
    lines.append("        return obj;")
    lines.append("    }")
    lines.append("};")
    lines.append("")

    lines.extend([
        namespace_close(schema_ns, 'objects'),
        "",
        guard_end,
    ])

    return '\n'.join(lines)


def qualify_type(f: Field, schema: Schema) -> str:
    """Get the fully qualified type for a field, adding namespace prefixes as needed."""
    if f.is_enum:
        return f"enums::{f.cpp_type}"
    elif f.is_object:
        return f.cpp_type  # Same namespace
    elif f.is_list:
        if f.list_element_type in schema.objects:
            return f'messages::List<{f.list_element_type}, {f.list_capacity}>'
        elif f.list_element_type in schema.enums:
            return f'messages::List<enums::{f.list_element_type}, {f.list_capacity}>'
        else:
            # Primitive
            elem_cpp = PRIMITIVE_TYPES[f.list_element_type][0]
            return f'messages::List<{elem_cpp}, {f.list_capacity}>'
    return f.cpp_type


def generate_message_file(msg: Message, schema: Schema) -> tuple[str, str]:
    """Generate header for Encoder/Decoder classes for a message."""
    class_name = msg.class_name
    decoder_name = f"{class_name}Decoder"
    encoder_name = f"{class_name}Encoder"
    category = 'commands' if msg.kind == 'command' else 'events'
    guard_begin, guard_end = header_guard(category, class_name)
    schema_ns = schema.namespace_parts

    # Collect dependencies
    enum_deps = set()
    obj_deps = set()
    for f in msg.fields:
        if f.is_enum:
            enum_deps.add(f.cpp_type)
        elif f.is_object:
            obj_deps.add(f.cpp_type)
        elif f.is_list:
            if f.list_element_type in schema.objects:
                obj_deps.add(f.list_element_type)
            elif f.list_element_type in schema.enums:
                enum_deps.add(f.list_element_type)

    needs_list = any(f.is_list for f in msg.fields)

    lines = [
        guard_begin,
        "#include <zbuffer/zbuffer.hpp>",
        "",
        "#include <cstdint>",
        "#include <cstdlib>",
        "#include <cstring>",
        "#include <ostream>",
        "#include <string_view>",
        "",
    ]

    # Include List if needed
    if needs_list:
        lines.append('#include <common/List.hpp>')

    # Include enum dependencies
    for dep in sorted(enum_deps):
        lines.append(f'#include "../enums/{dep}.hpp"')

    # Include object dependencies
    for dep in sorted(obj_deps):
        lines.append(f'#include "../objects/{dep}.hpp"')

    if needs_list or enum_deps or obj_deps:
        lines.append("")

    lines.extend([
        namespace_open(schema_ns, category),
        "",
        "// ============================================================================",
        f"// {decoder_name} - Zero-copy decoder, reads directly from ZBuffer",
        "// ============================================================================",
        "",
        f"class {decoder_name} {{",
        f"    zbuffer::ZBufferView view_;",
        "",
        "public:",
        f"    static constexpr std::uint32_t TYPE_ID = {msg.type_id};",
        f"    static constexpr std::size_t SIZE = {msg.size};",
        f"    static constexpr std::uint16_t VERSION = {msg.max_field_version};",
        f"    static constexpr const char* NAME = \"{class_name}\";",
        f"    static constexpr bool IS_COMMAND = {'true' if msg.kind == 'command' else 'false'};",
        f"    static constexpr bool IS_EVENT = {'true' if msg.kind == 'event' else 'false'};",
        "",
        f"    {decoder_name}() = default;",
        "",
        "    /**",
        "     * Wrap a read-only view for decoding, at its current read position.",
        "     * Does NOT advance it - caller must call skip() when done.",
        "     */",
        f"    void wrap(zbuffer::ZBufferView view) noexcept {{",
        f"        view_ = view.sliceReadable(0, SIZE);",
        "    }",
        "",
        "    /**",
        "     * Wrap a writable ZBuffer at its current read position.",
        "     */",
        f"    void wrap(zbuffer::ZBuffer& buffer) noexcept {{",
        f"        view_ = zbuffer::ZBufferView(buffer.read_ptr(), SIZE);",
        "    }",
        "",
        "    /**",
        "     * Skip past this message in the source buffer.",
        "     * Only works if wrap() was called with a reference to the source buffer.",
        "     */",
        "    static void skip(zbuffer::ZBufferView& view) noexcept {",
        "        view.skip(SIZE);",
        "    }",
        "",
        "    static void skip(zbuffer::ZBuffer& buffer) noexcept {",
        "        buffer.skip(SIZE);",
        "    }",
        "",
        "    /**",
        "     * Get raw data pointer (for zero-copy access).",
        "     */",
        "    const std::byte* data() const noexcept {",
        "        return reinterpret_cast<const std::byte*>(view_.data());",
        "    }",
        "",
        "    /**",
        "     * Get the underlying view.",
        "     */",
        "    const zbuffer::ZBufferView& view() const noexcept { return view_; }",
        "",
        "    // =========================================================================",
        "    // Getters - read directly from buffer (zero-copy)",
        "    // =========================================================================",
        "",
    ])

    # Generate getters for all fields
    offset = 0
    for f in msg.fields:
        lines.extend(generate_decoder_getter(f, offset))
        offset += f.size

    # Generate serialize method
    lines.append("")
    lines.append("    /**")
    lines.append("     * Serialize to output stream for debugging.")
    lines.append("     */")
    lines.append("    void serialize(std::ostream& os) const {")
    lines.append(f'        os << "{class_name}{{";')

    first = True
    for f in msg.fields:
        sep = "" if first else ","
        first = False
        getter_name = f"get{f.name[0].upper()}{f.name[1:]}"

        if f.is_string:
            lines.append(f'        os << "{sep}{f.name}=\\"" << {getter_name}AsString() << "\\"";')
        elif f.is_enum:
            lines.append(f'        os << "{sep}{f.name}=" << static_cast<std::int32_t>({getter_name}());')
        elif f.is_list:
            lines.append(f'        os << "{sep}{f.name}=[list]";')
        elif f.is_object:
            lines.append(f'        os << "{sep}{f.name}={{...}}";')
        else:
            lines.append(f'        os << "{sep}{f.name}=" << {getter_name}();')

    lines.append('        os << "}";')
    lines.append("    }")

    # Setters overwrite every byte of the field they own, so wrap() only has to
    # zero the ranges nothing can write: objects and lists, which have no setter.
    # Adjacent ranges are merged so this costs as few memsets as possible.
    uncovered: list[list[int]] = []
    _off = 0
    for f in msg.fields:
        has_setter = (f.name == 'messageVersion'
                      or f.is_primitive or f.is_enum or f.is_string)
        if not has_setter:
            if uncovered and uncovered[-1][0] + uncovered[-1][1] == _off:
                uncovered[-1][1] += f.size
            else:
                uncovered.append([_off, f.size])
        _off += f.size

    if uncovered:
        zero_lines = ["        // Zero the fields with no setter (objects, lists)"]
        zero_lines += [f"        std::memset(p + {start}, 0, {size});"
                       for start, size in uncovered]
    else:
        zero_lines = ["        // Every field has a setter that overwrites it in full"]

    lines.extend([
        "};",
        "",
        "// ============================================================================",
        f"// {encoder_name} - Writes directly to ZBuffer",
        "// ============================================================================",
        "",
        f"class {encoder_name} {{",
        f"    zbuffer::ZBuffer* buf_{{nullptr}};",
        f"    std::size_t offset_{{0}};",
        "",
        "public:",
        f"    static constexpr std::uint32_t TYPE_ID = {msg.type_id};",
        f"    static constexpr std::size_t SIZE = {msg.size};",
        f"    static constexpr std::uint16_t VERSION = {msg.max_field_version};",
        f"    static constexpr const char* NAME = \"{class_name}\";",
        "",
        f"    {encoder_name}() = default;",
        "",
        "    /**",
        "     * Wrap a ZBuffer for encoding. Writes at current write position.",
        "     * Initializes the VERSION field.",
        "     *",
        "     * Every field that has a setter must be set: wrap() no longer blanket-zeroes",
        "     * the message, so an unset settable field carries whatever the buffer held",
        "     * before. Fields with no setter (objects, lists) are still zeroed here.",
        "     *",
        "     * Call commit() when done to advance the buffer.",
        "     */",
        f"    void wrap(zbuffer::ZBuffer& buffer) noexcept {{",
        "        // An undersized encode buffer would corrupt the heap silently.",
        "        // Same policy as chronicle::Appender::write.",
        "        if (buffer.writable() < SIZE) [[unlikely]] {",
        "            std::abort();",
        "        }",
        "        buf_ = &buffer;",
        "        offset_ = buffer.write_pos();",
        "        auto* p = reinterpret_cast<std::byte*>(buffer.write_ptr());",
        "        // Initialize version",
        "        std::memcpy(p + 0, &VERSION, sizeof(VERSION));",
        *zero_lines,
        "    }",
        "",
        "    /**",
        "     * Commit the message - advances buffer write position by SIZE.",
        "     * Call after setting all fields.",
        "     */",
        "    void commit() noexcept {",
        "        if (buf_) buf_->advance_write(SIZE);",
        "    }",
        "",
        "    /**",
        "     * Get raw data pointer.",
        "     */",
        "    std::byte* data() noexcept {",
        "        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;",
        "    }",
        "    const std::byte* data() const noexcept {",
        "        return buf_ ? reinterpret_cast<const std::byte*>(buf_->data()) + offset_ : nullptr;",
        "    }",
        "",
        "    // =========================================================================",
        "    // Setters - write directly to buffer",
        "    // =========================================================================",
        "",
    ])

    # Generate setters for all fields (except messageVersion which is set in wrap())
    offset = 0
    for f in msg.fields:
        if f.name != 'messageVersion':  # VERSION is set automatically
            lines.extend(generate_encoder_setter(f, offset, schema))
        offset += f.size

    lines.extend([
        "};",
        "",
        namespace_close(schema_ns, category),
        "",
        guard_end,
    ])

    # No cpp file needed - all inline
    return '\n'.join(lines), ""


def generate_decoder_getter(f: Field, offset: int) -> list[str]:
    """Generate a getter method for the decoder."""
    getter_name = f"get{f.name[0].upper()}{f.name[1:]}"
    lines = []

    if f.is_primitive:
        lines.append(f"    [[nodiscard]] {f.cpp_type} {getter_name}() const noexcept {{")
        lines.append(f"        {f.cpp_type} v;")
        lines.append(f"        std::memcpy(&v, data() + {offset}, sizeof(v));")
        lines.append(f"        return v;")
        lines.append(f"    }}")
    elif f.is_enum:
        lines.append(f"    [[nodiscard]] enums::{f.cpp_type} {getter_name}() const noexcept {{")
        lines.append(f"        enums::{f.cpp_type} v;")
        lines.append(f"        std::memcpy(&v, data() + {offset}, sizeof(v));")
        lines.append(f"        return v;")
        lines.append(f"    }}")
    elif f.is_string:
        # Return ZBuffer slice
        lines.append(f"    [[nodiscard]] zbuffer::ZBufferView {getter_name}() const noexcept {{")
        lines.append(f"        return view_.slice({offset}, {f.string_len});")
        lines.append(f"    }}")
        lines.append("")
        # Convenience method returning string_view
        lines.append(f"    [[nodiscard]] std::string_view {getter_name}AsString() const noexcept {{")
        lines.append(f"        const char* p = reinterpret_cast<const char*>(data() + {offset});")
        lines.append(f"        return std::string_view(p, strnlen(p, {f.string_len}));")
        lines.append(f"    }}")
    elif f.is_object:
        lines.append(f"    [[nodiscard]] zbuffer::ZBufferView {getter_name}() const noexcept {{")
        lines.append(f"        return view_.slice({offset}, {f.size});")
        lines.append(f"    }}")
    elif f.is_list:
        lines.append(f"    [[nodiscard]] zbuffer::ZBufferView {getter_name}() const noexcept {{")
        lines.append(f"        return view_.slice({offset}, {f.size});")
        lines.append(f"    }}")

    lines.append("")
    return lines


def generate_encoder_setter(f: Field, offset: int, schema: Schema) -> list[str]:
    """Generate a setter method for the encoder."""
    setter_name = f"set{f.name[0].upper()}{f.name[1:]}"
    lines = []

    if f.is_primitive:
        lines.append(f"    void {setter_name}({f.cpp_type} v) noexcept {{")
        lines.append(f"        std::memcpy(data() + {offset}, &v, sizeof(v));")
        lines.append(f"    }}")
    elif f.is_enum:
        lines.append(f"    void {setter_name}(enums::{f.cpp_type} v) noexcept {{")
        lines.append(f"        std::memcpy(data() + {offset}, &v, sizeof(v));")
        lines.append(f"    }}")
    elif f.is_string:
        lines.append(f"    void {setter_name}(std::string_view s) noexcept {{")
        lines.append(f"        auto len = std::min(s.size(), std::size_t{{{f.string_len}}});")
        lines.append(f"        std::memcpy(data() + {offset}, s.data(), len);")
        lines.append(f"        if (len < {f.string_len}) std::memset(data() + {offset} + len, 0, {f.string_len} - len);")
        lines.append(f"    }}")
    elif f.is_object:
        lines.append(f"    // TODO: Object setter for {f.name}")
    elif f.is_list:
        lines.append(f"    // TODO: List setter for {f.name}")

    lines.append("")
    return lines


def qualify_type_for_message(f: Field, schema: Schema) -> str:
    """Get the fully qualified type for a field in a message."""
    if f.is_enum:
        return f"enums::{f.cpp_type}"
    elif f.is_object:
        return f"objects::{f.cpp_type}"
    elif f.is_list:
        if f.list_element_type in schema.objects:
            return f'messages::List<objects::{f.list_element_type}, {f.list_capacity}>'
        elif f.list_element_type in schema.enums:
            return f'messages::List<enums::{f.list_element_type}, {f.list_capacity}>'
        else:
            # Primitive
            elem_cpp = PRIMITIVE_TYPES[f.list_element_type][0]
            return f'messages::List<{elem_cpp}, {f.list_capacity}>'
    return f.cpp_type


def generate_field_encode(f: Field, offset: int, is_messageVersion: bool = False) -> list[str]:
    """Generate encoding code for a single field.

    Args:
        f: Field to encode
        offset: Byte offset in buffer
        is_messageVersion: If True, encode VERSION constant instead of field value
    """
    lines = []
    if is_messageVersion:
        # For messageVersion, encode the VERSION constant, not the field value
        lines.append(f"        std::memcpy(buf + {offset}, &VERSION, sizeof(VERSION));")
    elif f.is_primitive:
        lines.append(f"        std::memcpy(buf + {offset}, &{f.name}, sizeof({f.cpp_type}));")
    elif f.is_enum:
        lines.append(f"        std::memcpy(buf + {offset}, &{f.name}, sizeof(enums::{f.cpp_type}));")
    elif f.is_string:
        lines.append(f"        std::memcpy(buf + {offset}, {f.name}.data(), {f.string_len});")
    elif f.is_object:
        lines.append(f"        {f.name}.encode(buf + {offset});")
    elif f.is_list:
        lines.append(f"        {f.name}.encode(buf + {offset});")
    return lines


def generate_field_decode(f: Field, prefix: str, offset: int, check_version: bool = False) -> list[str]:
    """Generate decoding code for a single field.

    Args:
        f: Field to decode
        prefix: Variable prefix (e.g., "msg.")
        offset: Byte offset in buffer
        check_version: If True, wrap in version check (for fields added in later versions)
    """
    lines = []

    decode_line = None
    if f.is_primitive:
        decode_line = f"std::memcpy(&{prefix}{f.name}, buf + {offset}, sizeof({f.cpp_type}));"
    elif f.is_enum:
        decode_line = f"std::memcpy(&{prefix}{f.name}, buf + {offset}, sizeof(enums::{f.cpp_type}));"
    elif f.is_string:
        decode_line = f"std::memcpy({prefix}{f.name}.data(), buf + {offset}, {f.string_len});"
    elif f.is_object:
        decode_line = f"{prefix}{f.name} = {f.cpp_type}::decode(buf + {offset});"
    elif f.is_list:
        decode_line = f"{prefix}{f.name} = decltype({prefix}{f.name})::decode(buf + {offset});"

    if decode_line:
        if check_version and f.since_version > 1:
            lines.append(f"        if ({prefix}messageVersion >= {f.since_version}) {{")
            lines.append(f"            {decode_line}")
            lines.append(f"        }}")
        else:
            lines.append(f"        {decode_line}")

    return lines


def generate_list_template() -> str:
    return '''template<typename T, std::size_t Capacity>
struct List {
    static constexpr std::size_t ELEMENT_SIZE = sizeof(T);
    static constexpr std::size_t MAX_SIZE = Capacity;
    static constexpr std::size_t WIRE_SIZE = 4 + Capacity * sizeof(T);

    std::uint32_t count{0};
    std::array<T, Capacity> elements{};

    void clear() noexcept { count = 0; }
    bool push_back(const T& elem) noexcept {
        if (count >= Capacity) return false;
        elements[count++] = elem;
        return true;
    }
    T& operator[](std::size_t i) noexcept { return elements[i]; }
    const T& operator[](std::size_t i) const noexcept { return elements[i]; }
    std::size_t size() const noexcept { return count; }
    bool empty() const noexcept { return count == 0; }
    bool full() const noexcept { return count >= Capacity; }
    T* begin() noexcept { return elements.data(); }
    T* end() noexcept { return elements.data() + count; }
    const T* begin() const noexcept { return elements.data(); }
    const T* end() const noexcept { return elements.data() + count; }

    void encode(std::byte* buf) const noexcept {
        std::memcpy(buf, &count, sizeof(count));
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(buf + 4, elements.data(), count * sizeof(T));
        } else {
            for (std::uint32_t i = 0; i < count; ++i) {
                elements[i].encode(buf + 4 + i * sizeof(T));
            }
        }
    }

    static List decode(const std::byte* buf) noexcept {
        List list;
        std::memcpy(&list.count, buf, sizeof(list.count));
        if (list.count > Capacity) list.count = Capacity;
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(list.elements.data(), buf + 4, list.count * sizeof(T));
        } else {
            for (std::uint32_t i = 0; i < list.count; ++i) {
                list.elements[i] = T::decode(buf + 4 + i * sizeof(T));
            }
        }
        return list;
    }
};'''


def generate_list_file() -> str:
    """Generate List.hpp for the common folder."""
    lines = [
        "#ifndef MESSAGES_COMMON_LIST_HPP",
        "#define MESSAGES_COMMON_LIST_HPP",
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <cstring>",
        "#include <type_traits>",
        "",
        "namespace messages {",
        "",
        generate_list_template(),
        "",
        "} // namespace messages",
        "",
        "#endif // MESSAGES_COMMON_LIST_HPP",
        "",
    ]

    return '\n'.join(lines)


def generate_base_event_file() -> str:
    """Generate BaseEvent.hpp with common event fields."""
    return '''#ifndef MESSAGES_COMMON_BASE_EVENT_HPP
#define MESSAGES_COMMON_BASE_EVENT_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace messages {

/**
 * Base class for all events.
 * Contains common fields: messageVersion, sequenceNumber, msgSender, sendingTimestamp
 * 
 * messageVersion is encoded first so decoders can check compatibility before
 * reading the rest of the message. Fields are additive - newer versions only
 * add fields, never remove or modify existing ones.
 */
class BaseEvent {
public:
    static constexpr std::size_t BASE_SIZE = 50;  // 2 + 8 + 32 + 8
    
    std::uint16_t messageVersion{1};
    std::uint64_t sequenceNumber{0};
    std::array<char, 32> msgSender{};
    std::uint64_t sendingTimestamp{0};
    
    /**
     * Get msgSender as string_view (trims null bytes).
     */
    std::string_view getMsgSender() const noexcept {
        std::size_t len = 0;
        while (len < msgSender.size() && msgSender[len] != '\\0') ++len;
        return std::string_view(msgSender.data(), len);
    }
    
    /**
     * Set msgSender from string.
     */
    void setMsgSender(std::string_view sender) noexcept {
        std::size_t len = std::min(sender.size(), msgSender.size());
        std::memcpy(msgSender.data(), sender.data(), len);
        if (len < msgSender.size()) {
            std::memset(msgSender.data() + len, 0, msgSender.size() - len);
        }
    }

protected:
    /**
     * Encode base fields to buffer.
     * @param version The message version to encode
     */
    void encodeBase(std::byte* buf, std::uint16_t version) const noexcept {
        std::memcpy(buf + 0, &version, sizeof(version));
        std::memcpy(buf + 2, &sequenceNumber, sizeof(sequenceNumber));
        std::memcpy(buf + 10, msgSender.data(), 32);
        std::memcpy(buf + 42, &sendingTimestamp, sizeof(sendingTimestamp));
    }
    
    /**
     * Decode base fields from buffer.
     */
    void decodeBase(const std::byte* buf) noexcept {
        std::memcpy(&messageVersion, buf + 0, sizeof(messageVersion));
        std::memcpy(&sequenceNumber, buf + 2, sizeof(sequenceNumber));
        std::memcpy(msgSender.data(), buf + 10, 32);
        std::memcpy(&sendingTimestamp, buf + 42, sizeof(sendingTimestamp));
    }
};

} // namespace messages

#endif // MESSAGES_COMMON_BASE_EVENT_HPP
'''


def generate_base_command_file() -> str:
    """Generate BaseCommand.hpp with common command fields."""
    return '''#ifndef MESSAGES_COMMON_BASE_COMMAND_HPP
#define MESSAGES_COMMON_BASE_COMMAND_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace messages {

/**
 * Base class for all commands.
 * Contains common fields: messageVersion, sequenceNumber, msgSender, sendingTimestamp
 * 
 * messageVersion is encoded first so decoders can check compatibility before
 * reading the rest of the message. Fields are additive - newer versions only
 * add fields, never remove or modify existing ones.
 */
class BaseCommand {
public:
    static constexpr std::size_t BASE_SIZE = 50;  // 2 + 8 + 32 + 8
    
    std::uint16_t messageVersion{1};
    std::uint64_t sequenceNumber{0};
    std::array<char, 32> msgSender{};
    std::uint64_t sendingTimestamp{0};
    
    /**
     * Get msgSender as string_view (trims null bytes).
     */
    std::string_view getMsgSender() const noexcept {
        std::size_t len = 0;
        while (len < msgSender.size() && msgSender[len] != '\\0') ++len;
        return std::string_view(msgSender.data(), len);
    }
    
    /**
     * Set msgSender from string.
     */
    void setMsgSender(std::string_view sender) noexcept {
        std::size_t len = std::min(sender.size(), msgSender.size());
        std::memcpy(msgSender.data(), sender.data(), len);
        if (len < msgSender.size()) {
            std::memset(msgSender.data() + len, 0, msgSender.size() - len);
        }
    }

protected:
    /**
     * Encode base fields to buffer.
     * @param version The message version to encode
     */
    void encodeBase(std::byte* buf, std::uint16_t version) const noexcept {
        std::memcpy(buf + 0, &version, sizeof(version));
        std::memcpy(buf + 2, &sequenceNumber, sizeof(sequenceNumber));
        std::memcpy(buf + 10, msgSender.data(), 32);
        std::memcpy(buf + 42, &sendingTimestamp, sizeof(sendingTimestamp));
    }
    
    /**
     * Decode base fields from buffer.
     */
    void decodeBase(const std::byte* buf) noexcept {
        std::memcpy(&messageVersion, buf + 0, sizeof(messageVersion));
        std::memcpy(&sequenceNumber, buf + 2, sizeof(sequenceNumber));
        std::memcpy(msgSender.data(), buf + 10, 32);
        std::memcpy(&sendingTimestamp, buf + 42, sizeof(sendingTimestamp));
    }
};

} // namespace messages

#endif // MESSAGES_COMMON_BASE_COMMAND_HPP
'''


def generate_category_all_header(category: str, names: list[str], schema: Schema) -> str:
    """Generate all.hpp for a category (enums, objects, commands, events)."""
    guard_begin, guard_end = header_guard(category, 'all')

    lines = [guard_begin]

    for name in sorted(names):
        lines.append(f'#include "{name}.hpp"')

    lines.extend(["", guard_end])
    return '\n'.join(lines)


def generate_schema_all_header(schema: Schema) -> str:
    """Generate all.hpp for a single schema (includes all categories)."""
    guard_begin, guard_end = header_guard(schema.name, 'all')

    lines = [guard_begin]

    if schema.enums:
        lines.append('#include "enums/all.hpp"')
    if schema.objects:
        lines.append('#include "objects/all.hpp"')

    # Commands and events
    commands = [m for m in schema.messages.values() if m.kind == 'command']
    events = [m for m in schema.messages.values() if m.kind == 'event']

    if commands:
        lines.append('#include "commands/all.hpp"')
    if events:
        lines.append('#include "events/all.hpp"')

    # Handlers
    if commands or events:
        lines.append('#include "handlers/all.hpp"')

    lines.extend(["", guard_end])
    return '\n'.join(lines)


def generate_root_all_header(schemas: list[Schema]) -> str:
    """Generate root all.hpp that includes all schemas."""
    lines = [
        "#ifndef MESSAGES_ALL_HPP",
        "#define MESSAGES_ALL_HPP",
        "",
    ]

    for schema in schemas:
        if schema.namespace_parts:
            path = '/'.join(schema.namespace_parts)
            lines.append(f'#include "{path}/all.hpp"')
        else:
            # Shouldn't happen, but handle root schemas
            lines.append('#include "enums/all.hpp"')
            lines.append('#include "objects/all.hpp"')
            lines.append('#include "commands/all.hpp"')
            lines.append('#include "events/all.hpp"')

    lines.extend(["", "#endif // MESSAGES_ALL_HPP", ""])
    return '\n'.join(lines)


def generate_cmake(schemas: list[Schema], cpp_files: list[str]) -> str:
    if cpp_files:
        sources = '\n    '.join(f'${{CMAKE_CURRENT_SOURCE_DIR}}/{f}' for f in cpp_files)
        return f'''# Messages library - generated from IDL
add_library(messages STATIC
    {sources}
)
target_include_directories(messages PUBLIC ${{CMAKE_CURRENT_SOURCE_DIR}}/include)
target_link_libraries(messages PUBLIC seq::zbuffer)
add_library(seq::messages ALIAS messages)
'''
    else:
        return '''# Messages library - header-only (all inline)
add_library(messages INTERFACE)
target_include_directories(messages INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(messages INTERFACE seq::zbuffer)
add_library(seq::messages ALIAS messages)
'''


# =============================================================================
# Handler and Dispatcher Generation
# =============================================================================

def generate_command_handler(schema: Schema) -> str:
    """Generate CommandHandler class with default throw implementations for each command."""
    schema_ns = schema.namespace_parts
    commands = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'command']

    if not commands:
        return ""

    guard = f"MESSAGES_{'_'.join(schema_ns).upper()}_HANDLERS_COMMAND_HANDLER_HPP"

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "../commands/all.hpp"',
        "",
        "#include <cstdint>",
        "#include <stdexcept>",
        "#include <string>",
        "",
    ]

    # Open namespace
    ns_str = '::'.join(schema_ns)
    lines.append(f"namespace messages::{ns_str}::handlers {{")
    lines.append("")

    # Forward declare EventWriter
    lines.append("// Forward declaration - defined in EventWriter.hpp")
    lines.append("class EventWriter;")
    lines.append("")

    # Exception class
    lines.append("/**")
    lines.append(" * Exception thrown when a command handler is not implemented.")
    lines.append(" */")
    lines.append("class CommandNotImplemented : public std::runtime_error {")
    lines.append("public:")
    lines.append('    explicit CommandNotImplemented(const std::string& cmd)')
    lines.append('        : std::runtime_error("Command not implemented: " + cmd) {}')
    lines.append("};")
    lines.append("")

    # Handler class
    lines.append("/**")
    lines.append(" * Command handler base class.")
    lines.append(" * ")
    lines.append(" * Override the methods for commands you want to handle.")
    lines.append(" * Unhandled commands throw CommandNotImplemented by default.")
    lines.append(" * ")
    lines.append(" * Handlers receive zero-copy Decoder references - read only the fields you need.")
    lines.append(" */")
    lines.append("class CommandHandler {")
    lines.append("public:")
    lines.append("    virtual ~CommandHandler() = default;")
    lines.append("")

    # Virtual method with default throw for each command
    for name, msg in commands:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        lines.append(f"    /**")
        lines.append(f"     * Handle {class_name}.")
        lines.append(f"     * @param cmd Zero-copy decoder wrapping the command buffer")
        lines.append(f"     * @param writer Event writer for emitting response events")
        lines.append(f"     */")
        lines.append(f"    virtual void on{name}(commands::{decoder_name}& /*cmd*/, EventWriter& /*writer*/) {{")
        lines.append(f'        throw CommandNotImplemented("{class_name}");')
        lines.append(f"    }}")
        lines.append("")

    lines.append("};")
    lines.append("")
    lines.append(f"}} // namespace messages::{ns_str}::handlers")
    lines.append("")
    lines.append(f"#endif // {guard}")
    lines.append("")

    return '\n'.join(lines)


def generate_dispatcher(schema: Schema) -> str:
    """Generate Dispatcher class that routes messages to handler methods."""
    schema_ns = schema.namespace_parts
    commands = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'command']

    if not commands:
        return ""

    guard = f"MESSAGES_{'_'.join(schema_ns).upper()}_HANDLERS_DISPATCHER_HPP"

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "CommandHandler.hpp"',
        '#include "../commands/all.hpp"',
        "",
        "#include <zbuffer/zbuffer.hpp>",
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "#include <stdexcept>",
        "",
    ]

    ns_str = '::'.join(schema_ns)
    lines.append(f"namespace messages::{ns_str}::handlers {{")
    lines.append("")

    # Dispatcher class
    lines.append("/**")
    lines.append(" * Command dispatcher.")
    lines.append(" * ")
    lines.append(" * Reads TYPE_ID from ZBuffer and routes to appropriate handler method.")
    lines.append(" * Uses zero-copy decoding - wraps buffer directly without copying.")
    lines.append(" */")
    lines.append("class Dispatcher {")
    lines.append("public:")
    lines.append("    explicit Dispatcher(CommandHandler& handler, EventWriter& writer)")
    lines.append("        : handler_(handler), writer_(writer) {}")
    lines.append("")

    # Dispatch by TYPE_ID, which the queue carries in the message header
    lines.append("    /**")
    lines.append("     * Dispatch a command with known TYPE_ID.")
    lines.append("     *")
    lines.append("     * TYPE_ID is not in the payload: it travels in the queue's MessageHeader,")
    lines.append("     * so read it from chronicle::MessageView::type_id().")
    lines.append("     *")
    lines.append("     * @param typeId The command type ID")
    lines.append("     * @param buffer view positioned at the start of the message")
    lines.append("     * @return true if command was handled")
    lines.append("     */")
    lines.append("    bool dispatchById(std::uint32_t typeId, zbuffer::ZBufferView& buffer) {")
    lines.append("        switch (typeId) {")

    for name, msg in commands:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        lines.append(f"            case commands::{decoder_name}::TYPE_ID: {{")
        lines.append(f"                commands::{decoder_name} decoder;")
        lines.append(f"                decoder.wrap(buffer);")
        lines.append(f"                handler_.on{name}(decoder, writer_);")
        lines.append(f"                commands::{decoder_name}::skip(buffer);  // Advance buffer past message")
        lines.append(f"                return true;")
        lines.append(f"            }}")

    lines.append("            default:")
    lines.append("                return false;")
    lines.append("        }")
    lines.append("    }")
    lines.append("")

    # Type ID constants for reference
    lines.append("    // Type ID constants")
    for name, msg in commands:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        lines.append(f"    static constexpr std::uint32_t {name.upper()}_TYPE_ID = commands::{decoder_name}::TYPE_ID;")
    lines.append("")

    lines.append("private:")
    lines.append("    CommandHandler& handler_;")
    lines.append("    EventWriter& writer_;")
    lines.append("};")
    lines.append("")

    lines.append(f"}} // namespace messages::{ns_str}::handlers")
    lines.append("")
    lines.append(f"#endif // {guard}")
    lines.append("")

    return '\n'.join(lines)


def generate_event_writer_interface(schema: Schema) -> str:
    """Generate EventWriter interface for emitting events."""
    schema_ns = schema.namespace_parts
    events = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'event']

    if not events:
        return ""

    guard = f"MESSAGES_{'_'.join(schema_ns).upper()}_HANDLERS_EVENT_WRITER_HPP"

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "../events/all.hpp"',
        "",
        "#include <zbuffer/zbuffer.hpp>",
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "",
    ]

    ns_str = '::'.join(schema_ns)
    lines.append(f"namespace messages::{ns_str}::handlers {{")
    lines.append("")

    lines.append("/**")
    lines.append(" * Abstract event writer interface.")
    lines.append(" * ")
    lines.append(" * Implement this to write events to the event stream.")
    lines.append(" * ")
    lines.append(" * Usage:")
    lines.append(" *   events::ExecutionReportEventEncoder encoder;")
    lines.append(" *   encoder.wrap(writer.buffer());")
    lines.append(" *   encoder.setSequenceNumber(writer.nextSequence());")
    lines.append(" *   encoder.setSendingTimestamp(writer.now());")
    lines.append(" *   // ... set other fields")
    lines.append(" *   writer.write(encoder);  // Writes TYPE_ID + message to event stream")
    lines.append(" */")
    lines.append("class EventWriter {")
    lines.append("public:")
    lines.append("    virtual ~EventWriter() = default;")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Get the encode buffer. Encoder wraps this to write fields.")
    lines.append("     */")
    lines.append("    virtual zbuffer::ZBuffer& buffer() = 0;")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Get next sequence number.")
    lines.append("     */")
    lines.append("    virtual std::uint64_t nextSequence() = 0;")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Get current timestamp (nanoseconds).")
    lines.append("     */")
    lines.append("    virtual std::uint64_t now() = 0;")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Write an event to the event stream.")
    lines.append("     * Writes TYPE_ID prefix followed by message data.")
    lines.append("     * Resets the encode buffer for next event.")
    lines.append("     */")
    lines.append("    template<typename EncoderT>")
    lines.append("    void write(EncoderT& encoder) {")
    lines.append("        encoder.commit();  // Advance encode buffer write position")
    lines.append("        writeToStream(EncoderT::TYPE_ID, encoder.data(), EncoderT::SIZE);")
    lines.append("        buffer().reset();  // Reset encode buffer for next event")
    lines.append("    }")
    lines.append("")
    lines.append("protected:")
    lines.append("    /**")
    lines.append("     * Write TYPE_ID and message data to event stream.")
    lines.append("     * Implementors write typeId (4 bytes) + data (size bytes) to Chronicle.")
    lines.append("     */")
    lines.append("    virtual void writeToStream(std::uint32_t typeId, const std::byte* data, std::size_t size) = 0;")
    lines.append("};")
    lines.append("")

    lines.append(f"}} // namespace messages::{ns_str}::handlers")
    lines.append("")
    lines.append(f"#endif // {guard}")
    lines.append("")

    return '\n'.join(lines)


def generate_event_handler_interface(schema: Schema) -> str:
    """Generate EventHandler interface for receiving events."""
    schema_ns = schema.namespace_parts
    events = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'event']

    if not events:
        return ""

    guard = f"MESSAGES_{'_'.join(schema_ns).upper()}_HANDLERS_EVENT_HANDLER_HPP"

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "../events/all.hpp"',
        "",
        "#include <zbuffer/zbuffer.hpp>",
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "",
    ]

    ns_str = '::'.join(schema_ns)
    lines.append(f"namespace messages::{ns_str}::handlers {{")
    lines.append("")

    lines.append("/**")
    lines.append(" * Abstract event handler interface.")
    lines.append(" * ")
    lines.append(" * Implement this to handle events from the event stream.")
    lines.append(" * Default implementations do nothing - override what you need.")
    lines.append(" * ")
    lines.append(" * Handlers receive zero-copy Decoder references - read only the fields you need.")
    lines.append(" */")
    lines.append("class EventHandler {")
    lines.append("public:")
    lines.append("    virtual ~EventHandler() = default;")
    lines.append("")

    # Dispatch by ID, which the queue carries in the message header
    lines.append("    /**")
    lines.append("     * Dispatch an event with known TYPE_ID.")
    lines.append("     *")
    lines.append("     * TYPE_ID is not in the payload: it travels in the queue's MessageHeader,")
    lines.append("     * so read it from chronicle::MessageView::type_id().")
    lines.append("     *")
    lines.append("     * @return true if event was handled, false if unknown type")
    lines.append("     */")
    lines.append("    bool dispatchById(std::uint32_t typeId, zbuffer::ZBufferView& buffer) {")
    lines.append("        switch (typeId) {")

    for name, msg in events:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        handler_name = f"on{name}"
        lines.append(f"        case events::{decoder_name}::TYPE_ID: {{")
        lines.append(f"            events::{decoder_name} decoder;")
        lines.append(f"            decoder.wrap(buffer);")
        lines.append(f"            {handler_name}(decoder);")
        lines.append(f"            events::{decoder_name}::skip(buffer);")
        lines.append(f"            return true;")
        lines.append(f"        }}")

    lines.append("        default:")
    lines.append("            return false;")
    lines.append("        }")
    lines.append("    }")
    lines.append("")

    # Virtual handlers
    for name, msg in events:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        handler_name = f"on{name}"
        lines.append(f"    virtual void {handler_name}(events::{decoder_name}& event) {{ (void)event; }}")

    lines.append("};")
    lines.append("")

    lines.append(f"}} // namespace messages::{ns_str}::handlers")
    lines.append("")
    lines.append(f"#endif // {guard}")
    lines.append("")

    return '\n'.join(lines)


def generate_stream_dumper(schema: Schema) -> str:
    """Generate StreamDumper class that serializes events to a file."""
    schema_ns = schema.namespace_parts
    events = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'event']

    if not events:
        return ""

    # Filter out @noDump events
    dumpable_events = [(name, msg) for name, msg in events if not msg.no_dump]

    guard = f"MESSAGES_{'_'.join(schema_ns).upper()}_HANDLERS_STREAM_DUMPER_HPP"

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "EventHandler.hpp"',
        "",
        "#include <fstream>",
        "#include <filesystem>",
        "#include <string>",
        "",
    ]

    ns_str = '::'.join(schema_ns)
    lines.append(f"namespace messages::{ns_str}::handlers {{")
    lines.append("")

    lines.append("/**")
    lines.append(" * Event handler that dumps events to a file.")
    lines.append(" * ")
    lines.append(" * Each event is serialized on its own line.")
    lines.append(" * Events marked with @noDump are skipped.")
    lines.append(" * ")
    lines.append(" * Usage:")
    lines.append(" *   StreamDumper dumper(\"/var/log/events.log\");")
    lines.append(" *   dumper.dispatch(buffer);")
    lines.append(" *   dumper.flush();  // Flush to disk")
    lines.append(" */")
    lines.append("class StreamDumper : public EventHandler {")
    lines.append("public:")
    lines.append("    explicit StreamDumper(const std::filesystem::path& output_path)")
    lines.append("        : output_path_(output_path) {")
    lines.append("        stream_.open(output_path_, std::ios::out | std::ios::app);")
    lines.append("    }")
    lines.append("")
    lines.append("    ~StreamDumper() override {")
    lines.append("        flush();")
    lines.append("    }")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Flush buffer to disk.")
    lines.append("     */")
    lines.append("    void flush() {")
    lines.append("        if (stream_.is_open()) {")
    lines.append("            stream_.flush();")
    lines.append("        }")
    lines.append("    }")
    lines.append("")

    # Override handlers for dumpable events
    for name, msg in dumpable_events:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        handler_name = f"on{name}"
        lines.append(f"    void {handler_name}(events::{decoder_name}& event) override {{")
        lines.append("        if (stream_.is_open()) {")
        lines.append("            event.serialize(stream_);")
        lines.append("            stream_ << '\\n';")
        lines.append("        }")
        lines.append("    }")
        lines.append("")

    # No-op handlers for @noDump events (explicitly override to do nothing)
    nodump_events = [(name, msg) for name, msg in events if msg.no_dump]
    for name, msg in nodump_events:
        class_name = msg.class_name
        decoder_name = f"{class_name}Decoder"
        handler_name = f"on{name}"
        lines.append(f"    // @noDump - intentionally does nothing")
        lines.append(f"    void {handler_name}(events::{decoder_name}& /*event*/) override {{}}")
        lines.append("")

    lines.append("private:")
    lines.append("    std::filesystem::path output_path_;")
    lines.append("    std::ofstream stream_;")
    lines.append("};")
    lines.append("")

    lines.append(f"}} // namespace messages::{ns_str}::handlers")
    lines.append("")
    lines.append(f"#endif // {guard}")
    lines.append("")

    return '\n'.join(lines)


def generate_handlers_all_header(schema: Schema) -> str:
    """Generate all.hpp for handlers folder."""
    schema_ns = schema.namespace_parts
    guard = f"MESSAGES_{'_'.join(schema_ns).upper()}_HANDLERS_ALL_HPP"

    commands = [m for m in schema.messages.values() if m.kind == 'command']
    events = [m for m in schema.messages.values() if m.kind == 'event']

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]

    if events:
        lines.append('#include "EventWriter.hpp"')
        lines.append('#include "EventHandler.hpp"')
        lines.append('#include "StreamDumper.hpp"')
    if commands:
        lines.append('#include "CommandHandler.hpp"')
        lines.append('#include "Dispatcher.hpp"')

    lines.extend([
        "",
        f"#endif // {guard}",
        "",
    ])

    return '\n'.join(lines)


# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description='Generate C++ message code from IDL files')
    parser.add_argument('--idl-dir', '-i', default='.',
                        help='Root directory to scan for .idl files (default: current dir)')
    parser.add_argument('--output', '-o', default='../libs/messages/',
                        help='Output directory for generated code')
    args = parser.parse_args()

    script_dir = Path(__file__).parent

    idl_dir = Path(args.idl_dir)
    if not idl_dir.is_absolute():
        idl_dir = script_dir / args.idl_dir

    output_dir = Path(args.output)
    if not output_dir.is_absolute():
        output_dir = script_dir / args.output

    print(f"Scanning {idl_dir} for .idl files...")
    schemas = scan_idl_files(idl_dir)

    if not schemas:
        print("No .idl files found!")
        return 1

    print(f"\nResolving types...")
    for schema in schemas:
        resolve_schema(schema, schemas)

    # Print summary
    for schema in schemas:
        ns = schema.namespace
        inline_count = sum(1 for m in schema.messages.values() if m.inline)
        outline_count = len(schema.messages) - inline_count
        print(f"  {ns}: {len(schema.enums)} enums, {len(schema.objects)} objects, "
              f"{len(schema.messages)} messages ({inline_count} inline, {outline_count} outline)")

    # Generate code
    print(f"\nGenerating code to {output_dir}...")

    # Clean up previous generation
    include_dir = output_dir / "include"
    src_dir = output_dir / "src"

    if include_dir.exists():
        import shutil
        shutil.rmtree(include_dir)
        print("  Cleaned: include/")
    if src_dir.exists():
        import shutil
        shutil.rmtree(src_dir)
        print("  Cleaned: src/")

    cpp_files = []

    # Generate common utilities (List.hpp for list<T, N> support)
    common_dir = include_dir / 'common'
    common_dir.mkdir(parents=True, exist_ok=True)
    (common_dir / "List.hpp").write_text(generate_list_file())
    print("  Generated: common/List.hpp")

    for schema in schemas:
        # Schema directory is directly under include/ (no extra messages/ level)
        schema_include = include_dir / '/'.join(schema.namespace_parts) if schema.namespace_parts else include_dir
        schema_src = src_dir / '/'.join(schema.namespace_parts) if schema.namespace_parts else src_dir

        # Create category directories
        enums_dir = schema_include / 'enums'
        objects_dir = schema_include / 'objects'
        commands_dir = schema_include / 'commands'
        events_dir = schema_include / 'events'
        commands_src_dir = schema_src / 'commands'
        events_src_dir = schema_src / 'events'

        # Generate enum files
        if schema.enums:
            enums_dir.mkdir(parents=True, exist_ok=True)
            for name, enum in schema.enums.items():
                (enums_dir / f"{name}.hpp").write_text(generate_enum_file(enum, schema))
            (enums_dir / "all.hpp").write_text(
                generate_category_all_header('enums', list(schema.enums.keys()), schema))

        # Generate object files
        if schema.objects:
            objects_dir.mkdir(parents=True, exist_ok=True)
            for name, obj in schema.objects.items():
                (objects_dir / f"{name}.hpp").write_text(generate_object_file(obj, schema))
            (objects_dir / "all.hpp").write_text(
                generate_category_all_header('objects', list(schema.objects.keys()), schema))

        # Generate command files
        commands = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'command']
        if commands:
            commands_dir.mkdir(parents=True, exist_ok=True)
            command_names = []
            for name, msg in commands:
                class_name = msg.class_name
                command_names.append(class_name)
                header, cpp = generate_message_file(msg, schema)
                (commands_dir / f"{class_name}.hpp").write_text(header)

                if cpp:
                    commands_src_dir.mkdir(parents=True, exist_ok=True)
                    ns_path = '/'.join(schema.namespace_parts) if schema.namespace_parts else ''
                    include_path = f"{ns_path}/commands/{class_name}.hpp" if ns_path else f"commands/{class_name}.hpp"
                    cpp_content = f'#include "{include_path}"\n\n{cpp}'
                    (commands_src_dir / f"{class_name}.cpp").write_text(cpp_content)

                    rel_path = f'src/{ns_path}/commands/{class_name}.cpp' if ns_path else f'src/commands/{class_name}.cpp'
                    cpp_files.append(rel_path)
                    print(f"  Generated: {rel_path}")

            (commands_dir / "all.hpp").write_text(
                generate_category_all_header('commands', command_names, schema))

        # Generate event files
        events = [(name, msg) for name, msg in schema.messages.items() if msg.kind == 'event']
        if events:
            events_dir.mkdir(parents=True, exist_ok=True)
            event_names = []
            for name, msg in events:
                class_name = msg.class_name
                event_names.append(class_name)
                header, cpp = generate_message_file(msg, schema)
                (events_dir / f"{class_name}.hpp").write_text(header)

                if cpp:
                    events_src_dir.mkdir(parents=True, exist_ok=True)
                    ns_path = '/'.join(schema.namespace_parts) if schema.namespace_parts else ''
                    include_path = f"{ns_path}/events/{class_name}.hpp" if ns_path else f"events/{class_name}.hpp"
                    cpp_content = f'#include "{include_path}"\n\n{cpp}'
                    (events_src_dir / f"{class_name}.cpp").write_text(cpp_content)

                    rel_path = f'src/{ns_path}/events/{class_name}.cpp' if ns_path else f'src/events/{class_name}.cpp'
                    cpp_files.append(rel_path)
                    print(f"  Generated: {rel_path}")

            (events_dir / "all.hpp").write_text(
                generate_category_all_header('events', event_names, schema))

        # Generate handler files
        handlers_dir = schema_include / 'handlers'
        has_commands = any(m.kind == 'command' for m in schema.messages.values())
        has_events = any(m.kind == 'event' for m in schema.messages.values())

        if has_commands or has_events:
            handlers_dir.mkdir(parents=True, exist_ok=True)

            if has_events:
                (handlers_dir / "EventWriter.hpp").write_text(generate_event_writer_interface(schema))
                (handlers_dir / "EventHandler.hpp").write_text(generate_event_handler_interface(schema))
                (handlers_dir / "StreamDumper.hpp").write_text(generate_stream_dumper(schema))

            if has_commands:
                (handlers_dir / "CommandHandler.hpp").write_text(generate_command_handler(schema))
                (handlers_dir / "Dispatcher.hpp").write_text(generate_dispatcher(schema))

            (handlers_dir / "all.hpp").write_text(generate_handlers_all_header(schema))
            print(f"  Generated: handlers/")

        # Generate schema's all.hpp
        (schema_include / "all.hpp").write_text(generate_schema_all_header(schema))

    # Generate CMakeLists.txt
    (output_dir / "CMakeLists.txt").write_text(generate_cmake(schemas, cpp_files))

    # Count generated files
    hpp_count = sum(
        len(s.enums) + len(s.objects) + len(s.messages) + 1  # +1 for schema all.hpp
        + (1 if s.enums else 0)    # enums/all.hpp
        + (1 if s.objects or any(any(f.is_list for f in m.fields) for m in s.messages.values()) else 0)  # objects/all.hpp + List.hpp
        + (1 if any(m.kind == 'command' for m in s.messages.values()) else 0)  # commands/all.hpp
        + (1 if any(m.kind == 'event' for m in s.messages.values()) else 0)    # events/all.hpp
        for s in schemas
    )

    cpp_count = len(cpp_files)

    print(f"\nDone!")
    print(f"  Generated {hpp_count} header files, {cpp_count} source files")
    if cpp_files:
        print(f"  Library type: STATIC (has outline messages)")
    else:
        print(f"  Library type: INTERFACE (all inline)")

    return 0


if __name__ == '__main__':
    sys.exit(main() or 0)