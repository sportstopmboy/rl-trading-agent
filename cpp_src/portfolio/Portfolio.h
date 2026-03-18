#ifndef PORTFOLIO_H
#define PORTFOLIO_H

#include "cpp_src/market/CallOption.h"
#include "cpp_src/portfolio/OptionPosition.h"
#include "cpp_src/portfolio/SPXPosition.h"
#include <vector>
#include <deque>
#include <numeric>

class Portfolio
{
private:
    double cash;                               // The liquid capital the AI has
    SPXPosition spxPosition;                   // How many shares the AI owns and at what price
    std::vector<OptionPosition> activeOptions; // The contracts the AI has traded

    double initialCash; // How much cash the AI had at the beginning of the simulation
    double previousNAV; // A variable to store the net asset value of the AI during the previous trading day

    std::deque<double> returnHistory;       // A deque holding the % return on the portfolio for the last 30 days
    int sharpeWindowSize = 30;              // 30-day rolling window
    static constexpr double EPSILON = 1e-4; // Math safety net to prevent division by zero

public:
    // Constructor
    // Creates the portfolio with the specified starting capital
    Portfolio(double startingCash);

    // Method which allows the AI to buy/sell a contract
    // (Returns true if the trade was successful, false if insufficient funds)
    bool tradeOption(const CallOption& contract, double price, int quantity);

    // Method which forces the AI to hedge its position
    // This functionality is forced to ensure that the AI trades volatility and not direction
    // (Returns true if the trade was successful, false if insufficient funds)
    bool hedgeNetDelta(double spxPrice, const std::vector<const CallOption*>& todaysCallOptions);

private:
    // Helper method which executes the trade order of the hedge net delta function
    // It trades the difference calculated in that function
    bool tradeSPX(double price, int quantity);

public:
    // Calculates the Sharpe Ratio
    // This ratio represents how consistently the AI creates positive returns
    // By using the Sharpe Ratio we encourage the AI to consistently make positive trades
    // This eliminates high volatility of the portfolio and instead focuses on consistent results
    double getRollingSharpeReward(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions);

    // Gets the net asset value (what the AI is worth)
    // The net asset value consists of:
    // - The AI's cash
    // - The value of the contracts the AI owns
    // - The value of the SPX shares the AI owns
    // Tracking this gives us the AI's rewards
    double getNetAssetValue(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions) const;
    
    // Given a call option, returns the number of contracts the AI owns/owes
    int getPositionQuantity(const CallOption* contract) const;

    // Market Maintenance
    // This function automatically processes any contracts which expire today
    // It settles them based on if they finished above the strike price
    // It adds that cash to the AI's portfolio
    void processExpirations(const std::string& todaysDate, double spxSettlementPrice);

    // RL Episode Management
    // Reset method for future episodes
    // Brings back the portfolio to its intial state
    void reset();
    
    // Returns the total profit/loss until this point
    // Calculated by getting the difference between the current NAV and the initial cash
    double getTotalPnL(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions) const;
    
    // Returns how much more or less the AI makes compared to just holding the SPX
    // Calculated by getting the difference between the AI's current NAV
    // and the value of the SPX if the AI bought as much it could at the beginning of the simulation
    double getRelativePnL(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions) const;

    // Getters
    double getCash() const;
    const SPXPosition& getSpxPosition() const;
    const std::vector<OptionPosition>& getActiveOptions() const;
};

#endif