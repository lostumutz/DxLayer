#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <functional>

inline double ClockSeconds()
{
    static const double frequency = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / frequency;
}

// How often the game hands over a frame, measured from the capture.
class FrameStats
{
public:
    static constexpr int kHistory = 240;

    void Add(double seconds)
    {
        if (last_ > 0)
        {
            gaps_[head_] = static_cast<float>((seconds - last_) * 1000.0);
            head_ = (head_ + 1) % kHistory;
            count_ = std::min(count_ + 1, kHistory);
        }
        last_ = seconds;
        ++frames_;
    }

    // The summary numbers are worked out twice a second, so they can be read rather than flicker.
    void Tick(double seconds)
    {
        if (windowStart_ == 0)
            windowStart_ = seconds;
        const double span = seconds - windowStart_;
        if (span < 0.5)
            return;

        fps_ = static_cast<float>(frames_ / span);
        frames_ = 0;
        windowStart_ = seconds;
        low_ = SlowestPercentFps();
    }

    float Fps() const { return fps_; }
    float FrameMs() const { return fps_ > 0 ? 1000.0f / fps_ : 0.0f; }
    float LowFps() const { return low_; }

    // Frame times in milliseconds, oldest first. Returns how many there are.
    int History(float* out) const
    {
        for (int i = 0; i < count_; ++i)
            out[i] = gaps_[(head_ - count_ + i + kHistory) % kHistory];
        return count_;
    }

    void Reset() { *this = FrameStats(); }

private:
    // The "1% low": the frame rate the slowest 1% of recent frames ran at.
    float SlowestPercentFps() const
    {
        if (count_ < 20)
            return 0.0f;
        std::array<float, kHistory> sorted;
        History(sorted.data());
        const int slowest = count_ / 100;
        std::nth_element(sorted.begin(), sorted.begin() + slowest, sorted.begin() + count_, std::greater<>());
        return sorted[slowest] > 0 ? 1000.0f / sorted[slowest] : 0.0f;
    }

    std::array<float, kHistory> gaps_{};
    int head_ = 0;
    int count_ = 0;
    double last_ = 0;
    double windowStart_ = 0;
    int frames_ = 0;
    float fps_ = 0;
    float low_ = 0;
};
