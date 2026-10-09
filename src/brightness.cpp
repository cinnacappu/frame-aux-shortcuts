// 明るさの切り替えの計算の実装。
#include "brightness.h"

#include <cmath>

double brightnessPercentToGain(double percent) {
    const double ratio = percent / 100.0;
    return percent < 100.0 ? std::pow(ratio, 2.2) : ratio;
}

double brightnessGainToPercent(double gain) {
    if (gain <= 0.0) return 0.0;
    return gain < 1.0 ? 100.0 * std::pow(gain, 1.0 / 2.2) : 100.0 * gain;
}

size_t nextBrightnessIndex(const std::vector<double>& levels, double currentGain) {
    if (levels.empty()) return 0;
    size_t nearest = 0;
    double best = INFINITY;
    for (size_t i = 0; i < levels.size(); ++i) {
        const double distance = std::fabs(brightnessPercentToGain(levels[i]) - currentGain);
        if (distance < best) {  // 同じ距離なら先の段（同じ % を 2 回書いたときも先のほうに合わせる）
            best = distance;
            nearest = i;
        }
    }
    return (nearest + 1) % levels.size();
}
