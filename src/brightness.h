// 明るさの切り替えの計算（スライダーの % と steamvr/analogGain の値の変換、次の段の選び方）。OpenVR を使わないので単体テストできる。
#pragma once

#include <cstddef>
#include <vector>

/**
 * 明るさのスライダーの % を steamvr/analogGain の値にする（SteamVR の設定画面と同じ式）。
 * 100% 未満は (%/100)^2.2、100% 以上は %/100。
 * @param percent スライダーの %
 * @return analogGain の値
 */
double brightnessPercentToGain(double percent);

/**
 * steamvr/analogGain の値をスライダーの % に戻す（brightnessPercentToGain の逆）。
 * 1 未満は 100 * 値^(1/2.2)、1 以上は 100 * 値。
 * @param gain analogGain の値
 * @return スライダーの %
 */
double brightnessGainToPercent(double gain);

/**
 * 今の analogGain に一番近い段の、次の段を選ぶ（最後の段の次は最初の段）。
 * @param levels 段の一覧（スライダーの %）。空でないこと
 * @param currentGain 今の analogGain
 * @return 次に書く段の index
 */
size_t nextBrightnessIndex(const std::vector<double>& levels, double currentGain);
