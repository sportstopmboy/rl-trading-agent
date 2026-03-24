#include "cpp_src/environment/Environment.h"
#include "cpp_src/market/Market.h"
#include <vector>
#include <limits>
#include <iostream>
#include <random>
#include "Environment.h"

using namespace std;

// The Environment constructor passes the cash directly to the Portfolio
Environment::Environment(double initialCash)
    : market(), portfolio(initialCash), isDone(false)
{
}

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
vector<double> Environment::getObservation(const vector<const CallOption*>& todaysCallOptions,
                                           double currentSpxPrice,
                                           double historicalVolatility,
                                           const InterestRate* todaysRates)
{
    // Pre-allocate exactly 796 slots too prevent dynamic memory reallocation penalties
    vector<double> state;
    state.reserve(796); // 9 * 11 * 8 + 4

    // Define the fixed grid coordinates
    const vector<int> targetDTEs{3, 7, 10, 14, 21, 30, 45, 60, 90};
    const vector<double> targetMoneyness{0.95, 0.96, 0.97, 0.98, 0.99, 1.00, 1.01, 1.02, 1.03, 1.04, 1.05};

    // Populate the 99 (9 x 11) grid slots
    for (const CallOption* contract : todaysGrid)
    {
        if (contract != nullptr)
        {
            // Get the 8 features
            double exactMoneyness = contract->getStrike() / currentSpxPrice;
            double exactDTEInYears = contract->getDaysToExpiration() / 365.0;
            double midPrice = contract->calculateMidPrice();
            
            double riskFreeRate = todaysRates ? todaysRates->calculateAnnualizedRate(contract->getDaysToExpiration()) : 0.0;
            double theoreticalPrice = contract->calculateTheoreticalPrice(riskFreeRate, historicalVolatility);
            
            double edgeRatio = (theoreticalPrice > 0.0001) ? (midPrice / theoreticalPrice) : 1.0;
            double spreadPct = (midPrice > 0.0) ? ((contract->getAsk() - contract->getBid()) / midPrice) : 0.0;

            // Push the 8 features
            state.push_back(exactMoneyness);
            state.push_back(exactDTEInYears);
            state.push_back(edgeRatio);
            state.push_back(contract->getDelta());
            state.push_back(contract->getGamma());
            state.push_back(contract->getTheta());
            state.push_back(contract->getRho());
            state.push_back(spreadPct);
        }
        else
        {
            // Failsafe for corrupted data days (although this should not happen)
            for (int i = 0; i < 8; i++) state.push_back(0.0);
        }
    }

    // Calculate and append the 4 global features
    // Global 1: 30-day histoical volatility
    state.push_back(historicalVolatility);

    // Global 2: The baseline 3-month risk free rate
    // Assume 0.0 if the rate can't be found
    double baselineRate = todaysRates ? todaysRates->calculateAnnualizedRate(90) : 0.0;
    state.push_back(baselineRate);
    
    // Global 3: Portfolio Net Delta
    // (Normalized by NAV so the AI understands risk relative to its size)
    double netDelta = 0.0;

    for (const OptionPosition& optionPosition : portfolio.getActiveOptions())
    {
        const CallOption& ownedContract = optionPosition.getContract();

        // Try to find the live delta for accuracy
        // But automatically initialize to delta when we bought the contract
        // This is the fall back value if we cannot find live delta
        double liveDelta = ownedContract.getDelta();

        for (const CallOption* liveOption : todaysCallOptions)
        {
            if (liveOption->getExpirationDate() == ownedContract.getExpirationDate()
                && liveOption->getStrike() == ownedContract.getStrike())
            {
                liveDelta = liveOption->getDelta();
                break;
            }
        }

        netDelta += optionPosition.getPositionDelta(liveDelta);
    }

    // Note: Net Delta includes the SPX shares hedging it. 
    // We add the SPX share quantity to the options delta to get the true directional exposure.
    double trueNetDelta = netDelta + portfolio.getSpxPosition().getNumShares();

    // Get the current net asset value of the portfolio
    double currentNAV = portfolio.getNetAssetValue(currentSpxPrice, todaysCallOptions);

    // Protect against dividing by zero
    // This however should not happen as that would mean the AI is bankrupt
    state.push_back(currentNAV > 0 ? (trueNetDelta / currentNAV) : 0.0);

    // Global 4: Available Buying Power %
    // Protect against dividing by zero
    // This however should not happen as that would mean the AI is bankrupt
    state.push_back(currentNAV > 0 ? (portfolio.getCash() / currentNAV) : 0.0);

    return state;
}

// Translates the AI's raw output array into physical buy/sell orders
// It uses the decimal output as a percentage of how much current buying power the AI wants to allocate to that option
void Environment::executeAgentAction(const vector<double>& actionWeights,
                                     double currentSpxPrice)
{
    // Protect against inconsistent action weights array size errors
    // Although it is practically impossi
    if (actionWeights.size() != todaysGrid.size()) {
        cerr << "FATAL ERROR: AI passed " << actionWeights.size() 
             << " actions, but grid requires " << todaysGrid.size() << ".\n";
        return; 
    }

    // Determine the maximum buying power per slot using the cached array
    double maxCapitalPerSlot = portfolio.getNetAssetValue(currentSpxPrice, currentCallOptions);

    // Iterate through the AI's decisions
    for (size_t i = 0; i < todaysGrid.size(); i++)
    {
        const CallOption* contract = todaysGrid[i];

        // If the grid slot is empty, or the AI's signal is effectively zero, skip
        if (contract == nullptr || abs(actionWeights[i]) < 0.01) continue;

        // Calculate the physical dollar amount the AI wants to park in this contract
        // actionWeights[i] refers to the AI's signal (Squeezed between -1.0 for max short, and 1.0 for max long)
        // This value it multiplied by 1% to indicate that 1% of cash can be allocated to one call option at a time
        double targetCapital = actionWeights[i] * maxCapitalPerSlot * 0.01;

        // Calculate the physical cost of one contract
        double contractCost = contract->calculateMidPrice() * 100;

        // Protect against corrupted rows (Although this should not normally happen)
        if (contractCost <= 0) continue;

        // Calculate the target quantity of contracts (truncated to a whole integer)
        int targetQuantity = (int)(targetCapital / contractCost);

        // The CBOE Liquidity Constraint
        // Prevent the AI from buying up the entire world's supply of penny options
        // Instead the AI is limited to holding a maximum of 100 long or short contracts at one time
        // This effectively creates two constraints on the AI's risk:
        // 1) Only 1% of the AI's cash can be used on a call option
        // 2) It can not hold more than 100 of the same contract
        if (targetQuantity > 100) targetQuantity = 100; 
        if (targetQuantity < -100) targetQuantity = -100;

        // Find out how many of this exact contract we currently own
        int currentQuantity = portfolio.getPositionQuantity(contract);

        // Calculate the difference between the number of contracts
        // Tells us what trade we actually need to make
        int tradeQuantity = targetQuantity - currentQuantity;

        // Execution and Action clamping safety net
        if (tradeQuantity != 0)
        {
            // Slippage factor implementation
            // We assume a slippage factor of 20% for simplicity
            // We use this factor to assume effective buys and asks
            // This is because market makers increase the spread at the end of the day to protect themselves
            double midPrice = contract->calculateMidPrice();
            double spread = contract->getAsk() - contract->getBid();

            // The Slippage Factor: 0.20 means we cross 20% of the spread from the mid-price
            double slippageFactor = 0.20; 
            double executionPrice = midPrice;

            // If we are buying contracts
            if (tradeQuantity > 0) 
            {
                // We pay slightly more than the mid-price (Effective Ask)
                executionPrice = midPrice + (spread * slippageFactor);
            }
            // If we are selling
            else if (tradeQuantity < 0) 
            {
                // We receive slightly less than the mid-price (Effective Bid)
                executionPrice = midPrice - (spread * slippageFactor);
            }

            // The portfolio attemps the physical math using the penalized execution price
            // If the trade violates margin or cash limits, the portfolio rejects it and returns false
            bool success = portfolio.tradeOption(*contract, executionPrice, tradeQuantity);

            // The Liquidation Fallback
            // If the full trade was rejected (likely due to short margin), but we own contracts,
            // override the AI and just forcefully liquidate the long position to free up the cash
            // Then proceed to fulfil the AI's desired order
            if (!success && currentQuantity > 0 && tradeQuantity < 0) 
            {
                // Step 1: Liquidate all longs
                // This brings currentQuantity to 0 and adds the proceeds to our cash balance
                portfolio.tradeOption(*contract, executionPrice, -currentQuantity);

                // Step 2: Fulfill the AI's original intent
                // Now that our cash balance is higher, attempt the short sale again
                // Since we currently own 0, to reach the target, we just trade the targetQuantity
                portfolio.tradeOption(*contract, executionPrice, targetQuantity);
            }
        }
    }
}

// Helper function to find specific contracts that perfectly match chosen DTEs and Strikes
// This uses a nearest neighbor search to populate the grid each day
void Environment::updateDailyGrid(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions)
{
    // Wipe yesterday's grid, but keep the 99 slots of memory reserved for maximum efficiency
    todaysGrid.clear();
    todaysGrid.reserve(99); 

    // Define the x coordinates (DTE)
    const vector<int> targetDTEs = {3, 7, 10, 14, 21, 30, 45, 60, 90};
    // Define the y coordinates (moneyness)
    const vector<double> targetMoneyness = {0.95, 0.96, 0.97, 0.98, 0.99, 1.00, 1.01, 1.02, 1.03, 1.04, 1.05};

    // Loop through the 99 coordinates and perform a nearest neighbor search on each one
    for (int dte : targetDTEs)
    {
        for (double targetMoney : targetMoneyness)
        {
            // Temporary variables to hold the best match and closest distance
            const CallOption* bestMatch = nullptr;
            double minScore = numeric_limits<double>::max();

            // The nearest neighbor search
            for (const CallOption* option : todaysCallOptions)
            {
                // Get the DTE difference and moneyness difference
                double dteDiff = abs(option->getDaysToExpiration() - dte);
                double moneyDiff = abs(option->getStrike() / currentSpxPrice - targetMoney);

                // We multiply the absolute value of the DTE difference by a massive weight
                // This forces the algorithm to match the expiration date as closely as possible
                // before it cares about the strike price
                double score = (dteDiff * 1000.0) + moneyDiff;

                // Check if this is the nearest neighbor
                if (score < minScore)
                {
                    minScore = score;
                    bestMatch = option;
                }
            }
            // Save the exact memory address to the master class array
            todaysGrid.push_back(bestMatch);
        }
    }
}

// Formula used to calculate the reward the AI will receive
// This formula SHOULD TECHNICALLY ensure that the AI learns to trade well (please please work)
double Environment::calculateReward(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions)
{
    // This formula is R(NAV_t) = ln(NAV_t / NAV_t-1) * 100
    // The 100 is a scale factor derived to keep the reward between [-1; 1]
    // This is because it's unlikely to see a change in NAV by more than 1%
    // Therefore we want to keep the reward function between [-1; 1] but also as close to [-1; 1] as possible
    // There is an epsilon check in the portfolio class
    return log(portfolio.getDailyReturn(currentSpxPrice,  todaysCallOptions)) * 100;
}

// Teleports the AI to a random days, clears the portfolio, and returns the first state
vector<double> Environment::reset()
{
    // Reset the portfolio
    portfolio.reset();

    // Define the Safe Zone
    // We subtract 2016 to ensure the AI has at least one full epsiode of trades available
    int maxSpawnIndex = market.getTotalTradingDays() - 2016;

    // Generate the random starting index using a random number generator
    random_device rd;
    mt19937 gen(rd());    
    uniform_int_distribution<> distrib(0, maxSpawnIndex);

    int randomStartIndex = distrib(gen);

    // Teleport the market
    market.reset(randomStartIndex);

    // Reset episode end flag
    isDone = false;

    // Fetch Day 0 data exactly once
    currentCallOptions = market.getTodaysCallOptions();

    // Build the Fixed Grid so executeAgentAction has targets
    updateDailyGrid(market.getCurrentSpxPrice(), currentCallOptions);

    // Hand the AI its first look at the new world
    return getObservation(currentCallOptions,
                          market.getCurrentSpxPrice(),
                          market.calculateHistoricalVolatility(),
                          market.getTodaysInterestRate());
}

// Executes the AI's trades, hedges, advances time, and calculates the reward
StepResult Environment::step(const vector<double>& agentActions)
{
    // Get today's data market data before time moves
    string todaysDate = market.getTodaysDate();
    double currentSpxPrice = market.getCurrentSpxPrice();

    // Execute: Translates agent action array into actual trades
    // We use the current call options and grid (generated by reset or the previous step)
    executeAgentAction(agentActions, currentSpxPrice);

    // Hedge: Force the delta hedge (hardcoded risk management)
    portfolio.hedgeNetDelta(currentSpxPrice, currentCallOptions);

    // Time Step: Advance Time
    market.endTradingDay();

    // Gather the new day's data
    string newDate = market.getTodaysDate();
    double newSpxPrice = market.getCurrentSpxPrice();

    // Fetch: Gather the new day's data exactly once
    currentCallOptions = market.getTodaysCallOptions();

    // Update: Rebuild the fixed grid so the AI's "eyes" and "hands" map to the new day
    updateDailyGrid(newSpxPrice, currentCallOptions);

    // Settle: Settle any options that expire this day
    portfolio.processExpirations(newDate, newSpxPrice);
    
    // Calculate and report back the current NAV for Tensorboard analysis
    double currentNAV = portfolio.getNetAssetValue(newSpxPrice, currentCallOptions);

    // We use this statement to set the isDone flag
    // It is allocated to true when the end of the simulation is reached
    // Either by bankrupcy or running out of data
    isDone = (currentNAV <= 1000 || newDate == Market::END_DATE);

    // Calculate the reward to feed the AI                                   
    double reward = calculateReward(newSpxPrice, currentCallOptions);

    // Create the step result struct
    StepResult result;

    // Add the observation array to the struct
    result.stateFeatures = getObservation(currentCallOptions, newSpxPrice, 
                                          market.calculateHistoricalVolatility(), 
                                          market.getTodaysInterestRate());
    result.currentNAV = currentNAV;
    result.isDone = isDone;
    result.reward = reward;

    return result;
}

// Getters for episode analysis
bool Environment::getIsDone() { return isDone; }