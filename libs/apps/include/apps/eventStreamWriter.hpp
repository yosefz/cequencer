#ifndef APPS_EVENT_STREAM_WRITER_HPP
#define APPS_EVENT_STREAM_WRITER_HPP

#include "sequencerApp.hpp"
#include <zbuffer/zbuffer.hpp>
#include <holocron/holocronQueue.hpp>

#include <cstdint>
#include <cstring>

namespace apps {

    /**
     * Event stream writer for sequencer applications.
     *
     * Writes events to the sequencer's event stream with TYPE_ID prefix.
     *
     * Template parameter EventWriterBase should be the generated EventWriter interface
     * (e.g., messages::schema::handlers::EventWriter).
     *
     * Usage:
     *   class MyEventWriter : public EventStreamWriter<handlers::EventWriter> {
     *   public:
     *       using EventStreamWriter::EventStreamWriter;
     *
     *       void write(const events::ExecutionReportEvent& event) override {
     *           writeEvent(event);
     *       }
     *       // ... other write overrides
     *   };
     */
    template<typename EventWriterBase>
    class EventStreamWriter : public EventWriterBase {
    public:
        explicit EventStreamWriter(SequencerApp& app)
            : app_(app)
            , buffer_(app.config().max_command_size * 2) {
        }

    protected:
        /**
         * Write any event type to the stream.
         * TYPE_ID travels in the queue's message header, not the payload.
         */
        template<typename EventT>
        void writeEvent(const EventT& event) {
            buffer_.reset();
            event.encode(buffer_);
            app_.sequencer().eventStream().write(buffer_, EventT::TYPE_ID);
        }

        SequencerApp& app() { return app_; }

    private:
        SequencerApp& app_;
        zbuffer::ZBuffer buffer_;
    };

} // namespace apps

#endif // APPS_EVENT_STREAM_WRITER_HPP