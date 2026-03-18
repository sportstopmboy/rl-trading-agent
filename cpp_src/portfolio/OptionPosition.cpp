#include "cpp_src/portfolio/OptionPosition.h"
#include <cmath>

using namespace std;

// Constructor
// Stores the contract data and initialises it with zero contracts owned
// To increase the number of owned contracts, we call the process trade function
OptionPosition::OptionPosition(const CallOption& contract)
    : contract(contract), avgCost(0.0), numContracts(0)
{
}

// Function to purchase or sell contracts
// If the AI trades this exact same contract again, update the position
double OptionPosition::processTrade(double price, int quantity)
{
    // Prevent against empty trades
    if (quantity == 0) return 0.0;

    // Are we adding to our current position direction?
    // (Buying more when already long, or shorting more when already short)
    if ((numContracts >= 0 && quantity > 0) || (numContracts <= 0 && quantity < 0))
    {
        int totalContracts = abs(numContracts) + abs(quantity);
        double totalCost = (abs(numContracts) * avgCost) + (abs(quantity) * price);
        avgCost = totalCost / totalContracts;
    }
    // Are we reducing our position, and crossing zero to the other side?
    // (e.g., we are short -5 shares, and we buy +10 shares)
    else if (abs(quantity) > abs(numContracts))
    {
        // The old position is completely wiped out
        // The new avgCost is simply the price of the newly established position
        avgCost = price;
    }
    // Are we just reducing our position without crossing zero?
    // (e.g., we have 10 contracts, we sell 5)
    // avgCost stays exactly the same. We just change the quantity
    // Update the actual inventory
    numContracts += quantity;

    // Return the cash flow to the Portfolio (- spending, + receiving) including the fee
    // The options multiplier (100 shares per contract) must be applied to the price
    return -(price * 100.0) * quantity - (FEE * abs(quantity));
}

// Get the current market value of this specific position
// Determined by multiplying the number of contracts we own (or owe) multiplied by their fair price
double OptionPosition::getMarketValue(double liveMidPrice) const
{
    return numContracts * liveMidPrice * 100;
}

// Get the delta of the entire position
// Calculated from the number of contracts (x 100) we own multiplied by the delta of each contract
double OptionPosition::getPositionDelta(double liveDelta) const
{
    return numContracts * liveDelta * 100.0;
}

// A crucial helper to check if this position matches a contract in the market
// A contract is deemed the same if:
// - The expiry date is the same
// - The strike price is the same
bool OptionPosition::isSameContract(const CallOption& otherContract) const
{
    return (contract.getStrike() == otherContract.getStrike() &&
            contract.getExpirationDate() == otherContract.getExpirationDate());
}

// Getters
double OptionPosition::getAvgCost() const { return avgCost; }

int OptionPosition::getNumContracts() const { return numContracts; }

const CallOption& OptionPosition::getContract() const { return contract; }