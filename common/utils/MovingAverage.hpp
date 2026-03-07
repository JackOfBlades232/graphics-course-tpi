#pragma once

template <class T, size_t WINDOW_SIZE>
class MovingAverageAccumulator
{
  T window[WINDOW_SIZE] = {};
  T average = T{0};
  size_t pointer = 0;

public:
  void addSample(T sample)
  {
    average += (sample - window[pointer]) / T{WINDOW_SIZE};
    window[pointer++] = sample;
    if (pointer >= WINDOW_SIZE)
      pointer = 0;
  }

  T getAvg() const { return average; }
};
