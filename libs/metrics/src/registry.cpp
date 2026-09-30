#include <metrics/registry.hpp>

#include <fstream>
#include <system_error>

using namespace metrics;

Registry::Registry(std::string appName,
                   const std::filesystem::path& directory,
                   std::uint64_t writeIntervalNanos)
    : app_name_(std::move(appName))
    , output_path_(directory / (app_name_ + ".txt"))
    , tmp_path_(directory / (app_name_ + ".txt.tmp"))
    , write_interval_ns_(writeIntervalNanos) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
}

bool Registry::writeIfDue(std::uint64_t nanos) {
    if (written_ && nanos - last_write_ns_ < write_interval_ns_) [[likely]] {
        return false;
    }
    write(nanos);
    last_write_ns_ = nanos;
    written_ = true;
    return true;
}

void Registry::write(std::uint64_t nanos) {
    std::ofstream out(tmp_path_, std::ios::out | std::ios::trunc);
    if (!out) {
        return;
    }

    for (const auto& metric : metrics_) {
        out << "app=" << app_name_
            << " metric=" << metric->name()
            << " type=" << metric->typeName()
            << " value=" << metric->sample(nanos)
            << " ts=" << nanos << '\n';
    }

    out.close();
    if (!out) {
        // Partial write. Leave the previous dump in place rather than publish a
        // truncated one.
        return;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path_, output_path_, ec);
}
