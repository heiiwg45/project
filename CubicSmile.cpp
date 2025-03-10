#include "CubicSmile.h"
#include "BSAnalytics.h"
#include <iostream>
#include <cmath>
#include <map>
#include <numeric>

CubicSmile CubicSmile::FitSmile(const std::vector<TickData> &volTickerSnap)
{
  double fwd, T, atmvol, bf25, rr25, bf10, rr10;
  // TODO (step 3): fit a CubicSmile that is close to the raw tickers
  // - make sure all tickData are on the same expiry and same underlying
  // - get latest underlying price from all tickers based on LastUpdateTimeStamp
  // - get time to expiry T
  // - fit the 5 parameters of the smile, atmvol, bf25, rr25, bf10, and rr10 using L-BFGS-B solver, to the ticker data
  // ....
  // after the fitting, we can return the resulting smile
  std::cout << volTickerSnap.size() << std::endl;

  std::map<datetime_t, int> expDate;
  for (const TickData &tickData : volTickerSnap)
  {
    expDate[tickData.ExpiryDate]++;
  }

  for (const auto &entry : expDate)
  {
    std::cout << "Unique date:  " << entry.first << " , count: " << entry.second << std::endl;
  }

  double undPriceSum = std::accumulate(volTickerSnap.begin(), volTickerSnap.end(), 0.0,
                                       [](double acc, const TickData &data)
                                       {
                                         return acc + data.UnderlyingPrice;
                                       });

  // use average since there are some slight difference of the timestamp which resulted in different underlying price
  fwd = undPriceSum / static_cast<double>(volTickerSnap.size());

  datetime_t currentTimeStamp(volTickerSnap[0].LastUpdateTimeStamp);
  datetime_t expiryDate = volTickerSnap[0].ExpiryDate;
  // getting the time to maturity
  T = expiryDate - currentTimeStamp;

  atmvol = GetATMVolatility(volTickerSnap, fwd);

  // we use quick delta: qd = N(log(F/K / (atmvol) / sqrt(T))
  double stdev = atmvol * sqrt(T);
  double k_qd90 = quickDeltaToStrike(0.9, fwd, stdev);
  double k_qd75 = quickDeltaToStrike(0.75, fwd, stdev);
  double k_qd25 = quickDeltaToStrike(0.25, fwd, stdev);
  double k_qd10 = quickDeltaToStrike(0.10, fwd, stdev);

  double vol90 = interpolateQuickDeltaIV(volTickerSnap, k_qd90, false);
  double vol75 = interpolateQuickDeltaIV(volTickerSnap, k_qd75, false);
  double vol25 = interpolateQuickDeltaIV(volTickerSnap, k_qd25, true);
  double vol10 = interpolateQuickDeltaIV(volTickerSnap, k_qd10, true);

  bf25 = ((vol75 + vol25) / 2.0) - atmvol;
  bf10 = ((vol90 + vol10) / 2.0) - atmvol;
  rr25 = vol25 - vol75;
  rr10 = vol10 - vol90;

  return CubicSmile(fwd, T, atmvol, bf25, rr25, bf10, rr10);
}

CubicSmile::CubicSmile(double underlyingPrice, double T, double atmvol, double bf25, double rr25, double bf10, double rr10)
{
  // convert delta marks to strike vol marks, setup strikeMarks, then call BUildInterp
  double v_qd90 = atmvol + bf10 - rr10 / 2.0;
  double v_qd75 = atmvol + bf25 - rr25 / 2.0;
  double v_qd25 = atmvol + bf25 + rr25 / 2.0;
  double v_qd10 = atmvol + bf10 + rr10 / 2.0;

  // we use quick delta: qd = N(log(F/K / (atmvol) / sqrt(T))
  double stdev = atmvol * sqrt(T);
  double k_qd90 = quickDeltaToStrike(0.9, underlyingPrice, stdev);
  double k_qd75 = quickDeltaToStrike(0.75, underlyingPrice, stdev);
  double k_qd25 = quickDeltaToStrike(0.25, underlyingPrice, stdev);
  double k_qd10 = quickDeltaToStrike(0.25, underlyingPrice, stdev);

  strikeMarks.push_back(std::pair<double, double>(k_qd90, v_qd90));
  strikeMarks.push_back(std::pair<double, double>(k_qd75, v_qd75));
  strikeMarks.push_back(std::pair<double, double>(underlyingPrice, atmvol));
  strikeMarks.push_back(std::pair<double, double>(k_qd25, v_qd25));
  strikeMarks.push_back(std::pair<double, double>(k_qd10, v_qd10));
  BuildInterp();
}

void CubicSmile::BuildInterp()
{
  int n = strikeMarks.size();
  // end y' are zero, flat extrapolation
  double yp1 = 0;
  double ypn = 0;
  y2.resize(n);
  vector<double> u(n - 1);

  y2[0] = -0.5;
  u[0] = (3.0 / (strikeMarks[1].first - strikeMarks[0].first)) *
         ((strikeMarks[1].second - strikeMarks[0].second) / (strikeMarks[1].first - strikeMarks[0].first) - yp1);

  for (int i = 1; i < n - 1; i++)
  {
    double sig = (strikeMarks[i].first - strikeMarks[i - 1].first) / (strikeMarks[i + 1].first - strikeMarks[i - 1].first);
    double p = sig * y2[i - 1] + 2.0;
    y2[i] = (sig - 1.0) / p;
    u[i] = (strikeMarks[i + 1].second - strikeMarks[i].second) / (strikeMarks[i + 1].first - strikeMarks[i].first) - (strikeMarks[i].second - strikeMarks[i - 1].second) / (strikeMarks[i].first - strikeMarks[i - 1].first);
    u[i] = (6.0 * u[i] / (strikeMarks[i + 1].first - strikeMarks[i - 1].first) - sig * u[i - 1]) / p;
  }

  double qn = 0.5;
  double un = (3.0 / (strikeMarks[n - 1].first - strikeMarks[n - 2].first)) *
              (ypn - (strikeMarks[n - 1].second - strikeMarks[n - 2].second) / (strikeMarks[n - 1].first - strikeMarks[n - 2].first));

  y2[n - 1] = (un - qn * u[n - 2]) / (qn * y2[n - 2] + 1.0);

  //  std::cout << "y2[" << n-1 << "] = " << y2[n-1] << std::endl;
  for (int i = n - 2; i >= 0; i--)
  {
    y2[i] = y2[i] * y2[i + 1] + u[i];
    //    std::cout << "y2[" << i << "] = " << y2[i] << std::endl;
  }
}

double CubicSmile::Vol(double strike)
{
  unsigned i;
  // we use trivial search, but can consider binary search for better performance
  for (i = 0; i < strikeMarks.size(); i++)
    if (strike < strikeMarks[i].first)
      break; // i stores the index of the right end of the bracket

  // extrapolation
  if (i == 0)
    return strikeMarks[i].second;
  if (i == strikeMarks.size())
    return strikeMarks[i - 1].second;

  // interpolate
  double h = strikeMarks[i].first - strikeMarks[i - 1].first;
  double a = (strikeMarks[i].first - strike) / h;
  double b = 1 - a;
  double c = (a * a * a - a) * h * h / 6.0;
  double d = (b * b * b - b) * h * h / 6.0;
  return a * strikeMarks[i - 1].second + b * strikeMarks[i].second + c * y2[i - 1] + d * y2[i];
}