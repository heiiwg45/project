#ifndef _BS_ANALYTICS
#define _BS_ANALYTICS

#include <cmath>
#include <vector>
#include <algorithm>

#include "Solver/RootSearcher.h"

enum OptionType
{
    Call,
    Put
};
double cnorm(double x)
{
    // constants
    double a1 = 0.254829592;
    double a2 = -0.284496736;
    double a3 = 1.421413741;
    double a4 = -1.453152027;
    double a5 = 1.061405429;
    double p = 0.3275911;
    int sign = 1;
    if (x < 0)
        sign = -1;
    x = fabs(x) / sqrt(2.0);
    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-x * x);
    return 0.5 * (1.0 + sign * y);
}

double invcnorm(double x)
{
    assert(x > 0 && x < 1);
    auto f = [&x](double v)
    { return cnorm(v) - x; };
    return rfbisect(f, 1e-6, 1e8, 1e-6);
}

double bsUndisc(OptionType optType, double k, double fwd, double T, double sigma)
{
    double sigmaSqrtT = sigma * std::sqrt(T);
    double d1 = std::log(fwd / k) / sigmaSqrtT + 0.5 * sigmaSqrtT;
    double d2 = d1 - sigmaSqrtT;
    double V_0;
    switch (optType)
    {
    case Call:
        V_0 = fwd * cnorm(d1) - k * cnorm(d2);
        break;
    case Put:
        V_0 = k * cnorm(-d2) - fwd * cnorm(-d1);
        break;
    default:
        throw "unsupported optionType";
    }
    return V_0;
}

// qd = N(log(F/K) / stdev), so K = F / exp((N^{-1}(qd) * stdev))
double quickDeltaToStrike(double qd, double fwd, double stdev)
{
    double inv = invcnorm(qd);
    return fwd / std::exp(inv * stdev);
}

double quickDeltaToStrike(double qd, double fwd, double atmvol, double T)
{
    double stdev = atmvol * sqrt(T);
    return quickDeltaToStrike(qd, fwd, stdev);
}

// double impliedVol(OptionType optionType, double k, double fwd, double T, double undiscPrice)
// {
//     auto f = [undiscPrice, optionType, k, fwd, T](double vol)
//     { return bsUndisc(optionType, k, fwd, T, vol) - undiscPrice; };
//     // finding the value of implied volatility that makes the equation f bs price and market price is same.
//     return rfbrent(f, 1e-4, 10, 1e-6);
// }

/// @brief using the smallest difference OTM call and put and take avg as guess for ATMVol
/// TODO: check for ATM options if present (this may be wrong implementaion KIV is the other interpolation is bad try to correct this)
/// @param volTickerSnap
/// @return
// double GetATMVolatility(const std::vector<TickData> &volTickerSnap)
// {
//     double minDiffCall = std::numeric_limits<double>::max();
//     double minDiffPut = std::numeric_limits<double>::max();

//     for (const TickData &tickData : volTickerSnap)
//     {
//         if (tickData.moneyness == Moneyness::OTM)
//         {
//             if (tickData.OptionType == "C")
//             {
//                 minDiffCall = std::min(minDiffCall, tickData.Strike - tickData.UnderlyingPrice);
//             }
//             else if (tickData.OptionType == "P")
//             {
//                 minDiffPut = std::min(minDiffPut, tickData.UnderlyingPrice - tickData.Strike);
//             }
//         }
//     }

//     double atmvol = (minDiffCall + minDiffPut) / 2.0;

//     return atmvol;
// }

double InterpolateATMVolatility(const std::vector<double> &strikes, const std::vector<double> &volatilities, double underlyingPrice)
{
    // Find the nearest strikes above and below the underlying price
    auto lower = std::lower_bound(strikes.begin(), strikes.end(), underlyingPrice);
    auto upper = std::upper_bound(strikes.begin(), strikes.end(), underlyingPrice);

    // Check if there are nearby strikes on both sides of the underlying price
    if (lower != strikes.end() && upper != strikes.begin())
    {
        double lowerStrike = *(lower - 1);
        double upperStrike = *upper;

        double lowerVolatility = volatilities[lower - strikes.begin() - 1];
        double upperVolatility = volatilities[upper - strikes.begin()];

        // Linearly interpolate the implied volatilities
        double weight = (underlyingPrice - lowerStrike) / (upperStrike - lowerStrike);
        double atmvol = lowerVolatility + weight * (upperVolatility - lowerVolatility);

        return atmvol;
    }

    // If there are no nearby strikes on both sides, return 0.0 or handle as needed
    return 0.0;
}

/// @brief this interpolate ATM VOL with both ITM and OTM option and taking the nearby strike to interpolate
/// @param volTickerSnap
/// @param underlyingPrice
/// @return
double GetATMVolatility(const std::vector<TickData> &volTickerSnap, double underlyingPrice)
{
    std::vector<double> nearbyStrikes;
    std::vector<double> impliedVolatilities;
    double nearbyStrikeRange = 3000.0;

    // Collect nearby strikes and their corresponding implied volatilities
    for (const TickData &tickData : volTickerSnap)
    {
        if (std::abs(tickData.Strike - underlyingPrice) <= nearbyStrikeRange && tickData.moneyness == Moneyness::OTM)
        {
            nearbyStrikes.push_back(tickData.Strike);
            impliedVolatilities.push_back(tickData.MarkIV);
        }
    }

    // Sort strikes in ascending order
    std::sort(nearbyStrikes.begin(), nearbyStrikes.end());

    // Interpolate implied volatilities to estimate ATM volatility
    double atmvol = InterpolateATMVolatility(nearbyStrikes, impliedVolatilities, underlyingPrice);

    return atmvol;
}

double interpolateIV(const TickData &option1, const TickData &option2, double strike)
{
    double weight = (strike - option1.Strike) / (option2.Strike - option1.Strike);
    double impliedVolatility = option1.MarkIV +
                               weight * (option2.MarkIV - option1.MarkIV);

    return impliedVolatility;
}

std::vector<TickData> filterOptions(const std::vector<TickData>& options, std::string optionType)
{
    std::vector<TickData> filteredOptions;

    for (const TickData& option : options)
    {
        if (option.OptionType == optionType) // check if it is C or P
        {
            filteredOptions.push_back(option);
        }
    }

    return filteredOptions;
}

double interpolateQuickDeltaIV(
    const std::vector<TickData> &volTickerSnap, double quickDeltaStrike, bool useCallOption)
{
    if (useCallOption)
    {
        std::vector<TickData> callOptions = filterOptions(volTickerSnap, "C");
        // Sort the call and put options based on strike
        std::sort(callOptions.begin(), callOptions.end(),
                  [](const TickData &a, const TickData &b){ return a.Strike < b.Strike; });
        // Find the nearest call and put options to the quick delta strike
        auto callOption = std::upper_bound(callOptions.begin(), callOptions.end(), quickDeltaStrike,
                                           [](double val, const TickData &option)
                                           { return val < option.Strike; });
        if (callOption != callOptions.end())
        {
            double callVolatility = interpolateIV(*callOption, *(callOption - 1), quickDeltaStrike);
            return callVolatility;
        }
    }
    else
    {
        std::vector<TickData> putOptions = filterOptions(volTickerSnap, "P");
        std::sort(putOptions.begin(), putOptions.end(),
                  [](const TickData &a, const TickData &b)
                  { return a.Strike < b.Strike; });

        auto putOption = std::lower_bound(putOptions.begin(), putOptions.end(), quickDeltaStrike,
                                          [](const TickData &option, double val)
                                          { return option.Strike < val; });

        double putVolatility = interpolateIV(*putOption, *(putOption - 1), quickDeltaStrike);
        return putVolatility;
    }

    // Handle the case when the nearest call or put option is not available for interpolation
    return 0.0; // or any other appropriate default value
}

#endif
