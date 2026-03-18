#ifndef OPTION_POSITION_H
#define OPTION_POSITION_H

#include "cpp_src/market/CallOption.h"

class OptionPosition
{
public:
    static constexpr double FEE = 1.5; // The transaction fee (per contract) for completing a trade

private:
    // Contract Data
    CallOption contract; // The specific contract the AI traded
    double avgCost;      // The average price the AI originally paid or received
    int numContracts;    // Positive = Long Position, Negative = Short Position

public:
    // Constructor
    // Stores the contract data and initialises it with zero contracts owned
    // To increase the number of owned contracts, we call the process trade function
    OptionPosition(const CallOption& contract);

    // Function to purchase or sell contracts
    // If the AI trades this exact same contract again, it updates the current position
    double processTrade(double price, int quantity);

    // Get the current market value of this specific position
    double getMarketValue(double liveMidPrice) const;

    // Get the delta of the entire position
    // Calculated from the number of contracts (x 100) we own multiplied by the delta of each contract
    double getPositionDelta(double liveDelta) const;

    // A crucial helper to check if this position matches a contract in the market
    // A contract is deemed the same if:
    // - The expiry date is the same
    // - The strike price is the same
    bool isSameContract(const CallOption& otherContract) const;

    // Getters
    double getAvgCost() const;
    int getNumContracts() const;
    const CallOption& getContract() const;
};

#endif