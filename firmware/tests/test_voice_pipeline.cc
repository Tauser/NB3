#include <deque>
#include <vector>

#include "nb_test.h"
#include "voice/audio_policy.h"
#include "voice/barge_in_filter.h"
#include "voice/voice_frames.h"

static void test_preroll_burst_preserves_order_with_busy_encoder() {
    BargeInFilter gate(60);
    gate.SetArmed(true);
    // Two live frames already waiting, and no consumer while the burst arrives.
    std::deque<int> queue = {-2, -1};
    const size_t capacity = NB_BARGE_ENCODE_QUEUE_FRAMES(60);
    auto sink = [&](std::vector<int16_t>&& pcm) {
        NB_CHECK(queue.size() < capacity);
        queue.push_back(pcm.back());
    };
    for (int id = 0; id < 25; ++id) {
        std::vector<int16_t> frame(960, id < 20 ? 0 : 8192);
        frame.back() = static_cast<int16_t>(id);
        gate.Process(std::move(frame), id >= 20, sink);
    }
    NB_CHECK_EQ_INT(11, queue.size());
    NB_CHECK_EQ_INT(-2, queue.front()); queue.pop_front();
    NB_CHECK_EQ_INT(-1, queue.front()); queue.pop_front();
    for (int id = 16; id < 25; ++id) {
        NB_CHECK_EQ_INT(id, queue.front()); queue.pop_front();
    }
    std::vector<int16_t> live(960, 8192);
    live.back() = 25;
    gate.Process(std::move(live), true, sink);
    NB_CHECK_EQ_INT(25, queue.front());
}

static void test_vad_tag_follows_speech_remainder() {
    VoiceFrames frames(960);  // 60 ms, while AFE supplies 32 ms / 512 samples
    std::vector<int16_t> silence(512, 0), speech(512, 1234);
    std::vector<bool> tags;
    std::vector<std::vector<int16_t>> emitted;
    auto sink = [&](std::vector<int16_t>&& pcm, bool tag) {
        tags.push_back(tag); emitted.push_back(std::move(pcm));
    };
    frames.Push(silence.data(), 512, false, sink);
    frames.Push(speech.data(), 512, true, sink);
    frames.Push(silence.data(), 512, false, sink);
    frames.Push(silence.data(), 512, false, sink);
    NB_CHECK_EQ_INT(2, tags.size());
    NB_CHECK(tags[0] && tags[1]);
    NB_CHECK_EQ_INT(1234, emitted[1][63]);
    NB_CHECK_EQ_INT(0, emitted[1][64]);
    frames.Push(silence.data(), 512, false, sink);
    frames.Push(silence.data(), 512, false, sink);
    NB_CHECK(!tags[2]);
}

static void test_playback_end_keeps_new_voice_but_session_reset_drops_it() {
    BargeInFilter gate(60);
    gate.SetArmed(true);
    int emitted = 0;
    auto sink = [&](std::vector<int16_t>&&) { ++emitted; };
    gate.Process(std::vector<int16_t>(960, 0), false, sink);
    gate.Process(std::vector<int16_t>(960, 8192), true, sink);
    NB_CHECK_EQ_INT(0, emitted);  // too short to interrupt
    gate.SetArmed(false);
    gate.Process(std::vector<int16_t>(960, 8192), true, sink);
    NB_CHECK_EQ_INT(3, emitted);  // voice onset retained when playback ended
    gate.SetArmed(true);
    gate.Process(std::vector<int16_t>(960, 8192), true, sink);
    gate.SetArmed(false);
    gate.Reset();  // a new session is different from the tail ending
    gate.Process(std::vector<int16_t>(960, 0), false, sink);
    NB_CHECK_EQ_INT(4, emitted);  // no old preroll in a new session
}

static void test_playback_end_does_not_flush_weak_echo() {
    BargeInFilter gate(60);
    gate.SetArmed(true);
    int emitted = 0;
    auto sink = [&](std::vector<int16_t>&&) { ++emitted; };
    for (int i = 0; i < 10; ++i) gate.Process(std::vector<int16_t>(960, 100), true, sink);
    gate.SetArmed(false);
    gate.Process(std::vector<int16_t>(960, 0), false, sink);
    NB_CHECK_EQ_INT(1, emitted);
}

static void test_large_fetch_and_reset_preserve_metadata() {
    VoiceFrames frames(10);
    std::vector<int16_t> speech(25, 7), silence(10, 0);
    int count = 0;
    auto speech_sink = [&](std::vector<int16_t>&& pcm, bool tag) {
        NB_CHECK(tag); NB_CHECK_EQ_INT(10, pcm.size()); ++count;
    };
    frames.Push(speech.data(), speech.size(), true, speech_sink);
    NB_CHECK_EQ_INT(2, count);
    frames.Reset();  // discard five old speech samples and their tag
    frames.Push(silence.data(), silence.size(), false,
                [&](std::vector<int16_t>&& pcm, bool tag) {
                    NB_CHECK(!tag); NB_CHECK_EQ_INT(0, pcm.front()); ++count;
                });
    NB_CHECK_EQ_INT(3, count);
}

int main() {
    NB_RUN(test_preroll_burst_preserves_order_with_busy_encoder);
    NB_RUN(test_vad_tag_follows_speech_remainder);
    NB_RUN(test_playback_end_keeps_new_voice_but_session_reset_drops_it);
    NB_RUN(test_playback_end_does_not_flush_weak_echo);
    NB_RUN(test_large_fetch_and_reset_preserve_metadata);
    NB_TEST_MAIN_END();
}
