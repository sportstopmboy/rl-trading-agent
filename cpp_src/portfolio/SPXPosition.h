#ifndef SPX_POSITION_H
#define SPX_POSITION_H

class SPXPosition
{
public:
    static constexpr double FEE = 0.005; // The transaction fee (per share) for completing a trade

private:
    double avgCost; // The average cost of a stock during trade process
    int numShares;  // The number of shares in the portfolio

public:
    // Constructor
    SPXPosition();

    // Core trading logic: Processes buys/sells and returns the cash impact
    // Processes both buys (positive qty) and sells (negative qty)
    // Returns the cash impact: negative value means cash spent, positive means cash received
    double processTrade(double price, int quantity);

    // Valuation: Calculates the current Mark-to-Market value of the inventory
    // Basically returns how much our SPX shares are worth
    double getMarketValue(double currentSpxPrice) const;

    // Getters
    double getAvgCost() const;
    int getNumShares() const;
};

#endif