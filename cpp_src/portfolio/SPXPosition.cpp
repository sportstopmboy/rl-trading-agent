#include "cpp_src/portfolio/SPXPosition.h"
#include <iostream>
#include <cmath>

using namespace std;

// CONSTRUCTOR
SPXPosition::SPXPosition()
    : avgCost(0.0),
      numShares(0)
{
}

// Processes both buys (positive qty) and sells (negative qty)
// Returns the cash impact: negative value means cash spent, positive means cash received
double SPXPosition::processTrade(double price, int quantity)
{
    if (quantity == 0) return 0.0;

    // Are we adding to our current position direction?
    // (Buying more when already long, or shorting more when already short)
    if ((numShares >= 0 && quantity > 0) || (numShares <= 0 && quantity < 0))
    {
        int totalShares = abs(numShares) + abs(quantity);
        double totalCost = (abs(numShares) * avgCost) + (abs(quantity) * price);
        avgCost = totalCost / totalShares;
    }
    // Are we reducing our position, and crossing zero to the other side?
    // (e.g., we are short -5 shares, and we buy +10 shares)
    else if (abs(quantity) > abs(numShares))
    {
        // The old position is completely wiped out.
        // The new avgCost is simply the price of the newly established position.
        avgCost = price;
    }
    // Are we just reducing our position without crossing zero?
    // (e.g., we have 10 shares, we sell 5).
    // avgCost stays exactly the same. We just change the quantity.
    // Update the actual inventory
    numShares += quantity;

    // Return the cash flow to the Portfolio (- spending, + receiving) including the fee
    return -(price)*quantity - FEE * abs(quantity);
}

// Valuation: Calculates the current Mark-to-Market value of the inventory
// Basically returns how much our SPX shares are worth
double SPXPosition::getMarketValue(double currentSpxPrice) const
{
    return currentSpxPrice * numShares;
}

// Getters
double SPXPosition::getAvgCost() const { return avgCost; }

int SPXPosition::getNumShares() const { return numShares; }