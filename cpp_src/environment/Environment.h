#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "cpp_src/market/Market.h"
#include "cpp_src/portfolio/Portfolio.h"
#include <vector>

// A simple struct to package what each step results in
struct StepResult
{
    std::vector<double> stateFeatures;
    double reward;
    bool isDone;
    double currentNAV;
};

// This class acts as a broker to the AI
// It has access to the market as well as the AI's portfolio
// It is responsible for ensuring that all runs smoothly
class Environment
{
private:
    // The simulated market
    Market market;
    // The AI's portfolio
    Portfolio portfolio;
    
    // Boolean to flag if the simulation is finished
    bool isDone;

    // Holds the fetched options to prevent double-consumption
    std::vector<const CallOption*> currentCallOptions;

    // The master array holding exactly 99 pointers for today's grid
    // This is a 9x11 grid which allocates call options to specific qualities
    // The two qualities are DTE (x-axis) and Moneyness (y-axis)
    // Moneyness is defined as the strike price divided by the SPX price
    // It searches for call options which are most similar to these two qualities
    // In essence, if we imagine a cartesian plane, this grid represents the points
    // which are closest to points we predefine
    std::vector<const CallOption*> todaysGrid;

public:
    // Constructor
    // Initializes the environment and loads the 14-year database into RAM
    Environment(double initialCash);

private:
    // Gets the normalized state array to the AI
    // The state array contains the 99 call options from today's grid
    // As well as 8 state features for each call option
    // These 8 state features are:
    // 1) Exact moneyness
    // 2) Exact DTE
    // 3) Edge ratio (Black-Scholes Theoretical Price : Actual Mid Price)
    // 4) Delta
    // 5) Gamma
    // 6) Theta
    // 7) Rho
    // 8) Spread Percentage
    // The array also contains 4 global features:
    // 1) 30-day histoical volatility
    // 2) The baseline 3-month risk free rate
    // 3) Portfolio Net Delta
    // 4) Available Buying Power %
    std::vector<double> getObservation(const std::vector<const CallOption*>& todaysCallOptions,
                                       double currentSpxPrice,
                                       double historicalVol,
                                       const InterestRate* todaysRates);

    // Translates the AI's raw output array into physical buy/sell orders
    // It uses the decimal output as a percentage of how much current buying power the AI wants to allocate to that option
    void executeAgentAction(const std::vector<double>& actionWeights,
                            double currentSpxPrice);

    // Helper function to find specific contracts that perfectly match chosen DTEs and Strikes
    // This uses a nearest neighbor search to populate the grid each day
    void updateDailyGrid(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions);

    // Formula used to calculate the reward the AI will receive
    // This formula SHOULD TECHNICALLY ensure that the AI learns to trade well (please please work)
    double calculateReward(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions);

public:
    // Teleports the AI to a random days, clears the portfolio, and returns the first state
    std::vector<double> reset();

    // Executes the AI's trades, hedges, advances time, and calculates the reward
    StepResult step(const std::vector<double>& agentActions);

    // Getters for episode analysis
    bool getIsDone();
};

#endif