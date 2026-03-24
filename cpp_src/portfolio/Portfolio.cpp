#include "cpp_src/portfolio/Portfolio.h"
#include "cpp_src/market/CallOption.h"
#include "cpp_src/portfolio/SPXPosition.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include "Portfolio.h"

using namespace std;

// Constructor
// Creates the portfolio with the specified starting capital
Portfolio::Portfolio(double startingCash)
    : cash(startingCash),
      spxPosition(), // Implicitly built with dummy zeros
      initialCash(startingCash),
      previousNAV(startingCash)

{
    // activeOptions starts naturally empty
}

// Method which allows the AI to buy/sell a contract
// (Returns true if the trade was successful, false if insufficient funds/margin)
bool Portfolio::tradeOption(const CallOption& contract, double price, int quantity)
{
    // If the AI is trying to purchase 0 contracts, we ignore it
    if (quantity == 0) return false;

    // Get the buying power of the AI
    double buyingPower = getBuyingPower();

    // Calculate the expected fee the AI will have to pay
    double fee = abs(quantity) * OptionPosition::FEE;

    // Scan the portfolio to find out if we already own/owe this contract
    int currentQuantity = 0;
    auto existingPosition = activeOptions.end(); // Keep a pointer to it so we don't have to search again

    for (auto it = activeOptions.begin(); it != activeOptions.end(); ++it)
    {
        if (it->isSameContract(contract))
        {
            // Get the amount of contracts we own/owe
            currentQuantity = it->getNumContracts();
            // Store the pointer to the position
            existingPosition = it;
            break;
        }
    }

    // The following if statements are the conditions we have to meet to execute the trade
    // If we are buying
    if (quantity > 0)
    {
        // Calculate the cost of the transaction
        double cost = (price * quantity * 100) + fee;

        // Are we buying to close a short?
        if (currentQuantity < 0)
        {
            // The Zero-Crossing Check
            // Are we buying to open a long position?
            if (quantity > abs(currentQuantity))
            {
                // We are closing the short opening a new long in one giant trade
                // We assume we can do this in one giant trade to save on fees
                // However, if we are not able to do this in one trade
                // there is logic to do this in two trades in the environment class
                // Therefore, calculate the individual quantities for the different position types
                int closingQty = abs(currentQuantity);
                int openingQty = quantity - closingQty;

                // Calculate the cost to perform each action
                double costToClose = (price * closingQty * 100) + (closingQty * OptionPosition::FEE);
                double costToOpen = (price * openingQty * 100) + (openingQty * OptionPosition::FEE);

                // Closing uses Cash, Opening uses Buying Power
                if (cash < costToClose || buyingPower < costToOpen) return false;
            }
            // Are we resetting our positio to zero?
            else
            {
                // Just closing a short normally
                if (cash < cost) return false;
            }
        }
        // Are we opening or adding to a long position?
        else
        {
            // Return if we have the money to do that
            if (buyingPower < cost) return false;
        }
    }
    // If we are selling
    else if (quantity < 0)
    {
        // Are we selling to close a long?
        if (currentQuantity > 0)
        {
            // The Zero-Crossing Check
            // Are we selling to open a short position?
            if (abs(quantity) > currentQuantity)
            {
                // We are liquidating the long and opening a new short in one giant trade
                // We assume we can do this in one giant trade to save on fees
                // However, if we are not able to do this in one trade
                // there is logic to do this in two trades in the environment class
                // Therefore, calculate the individual quantities for the different position types
                int closingQty = currentQuantity;
                int openingQty = abs(quantity) - closingQty;

                // Calculate the fee we will pay to close our current position
                double feeToClose = closingQty * OptionPosition::FEE;

                // Calculate the margin against the underlying SPX price
                double marginToOpen = (contract.getUnderlyingLast() * openingQty * 100) * 0.20 + (openingQty * OptionPosition::FEE);

                // Both liquidating and margining require Buying Power
                // Check if we have enough
                if (buyingPower < feeToClose + marginToOpen)
                    return false;
            }
            // Are we selling all of our long positions to return to a neutral state?
            else
            {
                // Just liquidating an asset
                // Check we have the buying power to pay the fee
                if (buyingPower < fee)
                    return false;
            }
        }

        // Are we opening or adding to a short position?
        else
        {
            // Calculate the margin against the underlying SPX price
            double underlyingPrice = contract.getUnderlyingLast();
            double initialMargin = (underlyingPrice * abs(quantity) * 100) * 0.20;

            // Return if we have the money to do that
            if (buyingPower < initialMargin + fee)
                return false;
        }
    }

    // Execute the actual physical trade
    // Do we already own/owe this contract?
    if (existingPosition != activeOptions.end())
    {
        // Update existing position
        cash += existingPosition->processTrade(price, quantity);

        // Clean up dead contracts
        if (existingPosition->getNumContracts() == 0)
        {
            activeOptions.erase(existingPosition);
        }
    }
    // Are we opening a new position?
    else
    {
        // Open brand new position
        activeOptions.push_back(OptionPosition(contract));
        cash += activeOptions.back().processTrade(price, quantity);
    }

    // Return the trade was successful
    return true;
}

// Method which forces the AI to hedge its position
// This functionality is forced to ensure that the AI trades volatility and not direction
// (Returns true if the trade was successful, false if insufficient funds)
bool Portfolio::hedgeNetDelta(double spxPrice, const vector<const CallOption*>& todaysCallOptions)
{
    // Temporary vairable to calculate the total directional exposure of the current portfolio
    double targetHedge = 0.0;

    // Loop through each option position we own, find the corresponding contract with today's parameters
    for (const OptionPosition& optionPosition : activeOptions)
    {
        const CallOption& ownedContract = optionPosition.getContract();
        bool foundLiveDelta = false;

        // Loop through today's call options to find the contract which matches
        for (const CallOption* liveOption : todaysCallOptions)
        {
            // Check our contract against the open call option using the composite key
            // Key = ExpirationDate, Strike
            if (liveOption->getExpirationDate() == ownedContract.getExpirationDate() && liveOption->getStrike() == ownedContract.getStrike())
            {
                // We pass the live delta into the getPositionDelta method
                targetHedge -= optionPosition.getPositionDelta(liveOption->getDelta());
                foundLiveDelta = true;
                break;
            }
        }

        // Fallback if the option didn't trade today
        // Use a brutal Binary Delta as a last resort
        if (!foundLiveDelta)
        {
            // 1.0 if it is In-The-Money, 0.0 if it is Out-Of-The-Money
            double fallbackDelta = (spxPrice > ownedContract.getStrike()) ? 1.0 : 0.0;

            targetHedge -= optionPosition.getPositionDelta(fallbackDelta);
        }
    }

    // Trade the difference of shares between current SPX position and the target position.
    return tradeSPX(spxPrice, (int)round(targetHedge - spxPosition.getNumShares()));
}

// Helper method which executes the trade order of the hedge net delta function
// It trades the difference calculated in that function
// Helper method which executes the trade order of the hedge net delta function
bool Portfolio::tradeSPX(double price, int quantity)
{
    // If the AI is trying to purchase 0 shares, we ignore it
    if (quantity == 0)
        return false;

    // Get the buying power of the AI and the cash
    // We ensure they are not negative to prevent math errors when dividing
    double buyingPower = max(0.0, getBuyingPower());
    double currentCash = max(0.0, cash);

    // Get the amount of SPX shares we currently own/owe
    int currentShares = spxPosition.getNumShares();

    // Calculate the costs per share to make the math easier later
    double costPerShareLong = price + SPXPosition::FEE;
    double feePerShare = SPXPosition::FEE;
    double marginPerShareShort = (price * 0.50) + SPXPosition::FEE;

    // The following if statements are the conditions we have to meet to execute the trade
    // If we are buying
    if (quantity > 0)
    {
        // Are we buying to close a short?
        if (currentShares < 0)
        {
            // The Zero-Crossing Check
            // Are we buying to open a long position?
            if (quantity > abs(currentShares))
            {
                // We are closing the short and opening a new long in one giant trade
                // Therefore, calculate the individual quantities for the different position types
                int closingQty = abs(currentShares);
                int openingQty = quantity - closingQty;

                // Calculate the cost to perform each action
                double costToClose = closingQty * costPerShareLong;
                double costToOpen = openingQty * costPerShareLong;

                // Closing uses Cash, Opening uses Buying Power
                // If we don't have enough money for the giant trade
                if (currentCash < costToClose || buyingPower < costToOpen)
                {
                    // Check if we can at least close the short
                    if (currentCash < costToClose)
                    {
                        // We don't have enough to close the whole short
                        // Just buy back as many as we can afford with our cash
                        quantity = (int)(currentCash / costPerShareLong);
                    }
                    else
                    {
                        // We can close the short, but can't fully open the long
                        // Buy as many longs as our buying power allows
                        openingQty = (int)(buyingPower / costPerShareLong);
                        quantity = closingQty + openingQty;
                    }
                }
            }
            // Are we resetting our position to zero?
            else
            {
                // Calculate the cost of the transaction
                double cost = quantity * costPerShareLong;

                // Check if we have the balance to do that
                if (currentCash < cost)
                {
                    // Buy back as many short shares as our current cash allows
                    quantity = (int)(currentCash / costPerShareLong);
                }
            }
        }
        // Are we opening or adding to a long position?
        else
        {
            // Calculate the cost of the transaction
            double cost = quantity * costPerShareLong;

            // Return if we have the money to do that
            if (buyingPower < cost)
            {
                // Buy as many long shares as our buying power allows
                quantity = (int)(buyingPower / costPerShareLong);
            }
        }
    }
    // If we are selling
    else if (quantity < 0)
    {
        // Are we selling to close a long?
        if (currentShares > 0)
        {
            // The Zero-Crossing Check
            // Are we selling to open a short position?
            if (abs(quantity) > currentShares)
            {
                // We are liquidating the long and opening a new short in one giant trade
                // Therefore, calculate the individual quantities for the different position types
                int closingQty = currentShares;
                int openingQty = abs(quantity) - closingQty;

                // Calculate the fee we will pay to close our current position
                double feeToClose = closingQty * feePerShare;

                // SPX requires 50% Reg-T Initial Margin
                double marginToOpen = openingQty * marginPerShareShort;

                // Both liquidating and margining require Buying Power
                // Check if we have enough
                if (buyingPower < feeToClose + marginToOpen)
                {
                    // Check if we can at least pay the fees to close the longs
                    if (buyingPower < feeToClose)
                    {
                        // We don't have enough to close all longs
                        // Sell as many as we can afford the exit fee for
                        int maxClose = (int)(buyingPower / feePerShare);
                        quantity = -maxClose;
                    }
                    else
                    {
                        // We can close all longs, but can't fully open the shorts
                        // Use whatever buying power is left for margin
                        double remainingBP = buyingPower - feeToClose;
                        openingQty = (int)(remainingBP / marginPerShareShort);
                        quantity = -(closingQty + openingQty);
                    }
                }
            }
            // Are we selling all of our long positions to return to a neutral state?
            else
            {
                // Calculate the expected fee the AI will have to pay
                double totalFee = abs(quantity) * feePerShare;

                // Check we have the buying power to pay the fee
                if (buyingPower < totalFee)
                {
                    // Sell as many as we can afford to pay the fee for
                    quantity = -(int)(buyingPower / feePerShare);
                }
            }
        }
        // Are we opening or adding to a short position?
        else
        {
            // SPX requires 50% Reg-T Initial Margin
            double initialMargin = abs(quantity) * marginPerShareShort;

            // Return if we have the money to do that
            if (buyingPower < initialMargin)
            {
                // Short as many shares as our margin allows
                quantity = -(int)(buyingPower / marginPerShareShort);
            }
        }
    }

    // If the AI is completely broke and the quantity dropped to 0, reject the trade
    if (quantity == 0) return false;

    // Execute the actual physical trade
    // Update the Portfolio's bank account
    cash += spxPosition.processTrade(price, quantity);

    // Return the trade was successful
    return true;
}

// Calculates the daily return expressed as a ratio
// The ratio is NAV today : NAV yesterday
double Portfolio::getDailyReturn(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions)
{
    // Get the current net asset value
    double currentNAV = getNetAssetValue(currentSpxPrice, todaysCallOptions);

    // Calculate the return ratio
    double ratio = currentNAV / previousNAV;

    // Update the previous NAV
    previousNAV = currentNAV;

    // Return the ratio
    // Evaluate if the ratio is too small to prevent gradient explosion
    return ratio > EPSILON ? ratio : EPSILON;
}

// Gets the net asset value (what the AI is worth)
// The net asset value consists of:
// - The AI's cash
// - The value of the contracts the AI owns
// - The value of the SPX shares the AI owns
// Tracking this gives us the AI's rewards
double Portfolio::getNetAssetValue(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions) const
{
    // Declare a variable to hold the net asset value.
    // From the beginning, initialize it with the amount of cash in the portfolio.
    double netAssetValue = cash;

    // Calculate the value of our SPX equity and add to the net asset value
    netAssetValue += spxPosition.getNumShares() * currentSpxPrice;

    // Find the value of each contract held in the portfolio
    for (const OptionPosition& optionPosition : activeOptions)
    {
        bool foundLivePrice = false;
        const CallOption& ownedContract = optionPosition.getContract();

        // Search today's call option contracts for the exact same contract
        for (const CallOption* liveOption : todaysCallOptions)
        {
            // Checks current held contract against each live option to check if they have the same composite key
            // The composite key is formed using the expiration date and the strike price
            if (liveOption->getExpirationDate() == ownedContract.getExpirationDate() && liveOption->getStrike() == ownedContract.getStrike())
            {
                // Get the mid price and use that to calculate contract value
                // Add to NAV (Negative quantities natively subtract from the total)
                netAssetValue += optionPosition.getNumContracts() * liveOption->calculateMidPrice() * 100.0;

                foundLivePrice = true;
                break; // Stop searching once we find the exact match
            }
        }

        // What if the option didn't trade today and is missing?
        if (!foundLivePrice)
        {
            // Fallback to Intrinsic Value, not Average Cost.
            // If the market crashes, deep OTM options go to 0.0, exposing the liability.
            double strike = ownedContract.getStrike();
            double intrinsicValue = (currentSpxPrice > strike) ? (currentSpxPrice - strike) : 0.0;

            // Add the intrinsic value to NAV
            // (Note: Negative quantities will natively subtract this liability from the total)
            netAssetValue += optionPosition.getNumContracts() * intrinsicValue * 100.0;
        }
    }

    return netAssetValue;
}

// Given a call option, returns the number of contracts the AI owns/owes
int Portfolio::getPositionQuantity(const CallOption* contract) const
{
    // Safety check: Ensure the pointer isn't null
    if (contract == nullptr) return 0;

    // Loop through our current positions to determine if we own this particular option
    // And if so, how many of those options
    for (const OptionPosition& optionPosition : activeOptions)
    {
        const CallOption& ownedContract = optionPosition.getContract();

        // Compare the Composite Key
        if (ownedContract.getExpirationDate() == contract->getExpirationDate() && ownedContract.getStrike() == contract->getStrike())
        {
            // Return the number of contracts
            return optionPosition.getNumContracts();
        }
    }

    // If the loop was unsuccessful at finding the contract, return that we do not own it
    return 0;
}

// Market Maintenance
// This function automatically processes any contracts which expire today
// It settles them based on if they finished above the strike price
// It adds that cash to the AI's portfolio
void Portfolio::processExpirations(const string& todaysDate, double spxSettlementPrice)
{
    // We use an iterator starting at the beginning
    auto it = activeOptions.begin();

    // We use an iterator starting at the beginning
    while (it != activeOptions.end())
    {
        // Check if the contract expires today
        if (it->getContract().getExpirationDate() <= todaysDate)
        {
            // Calculate the Intrinsic Value of the Call Option
            double strikePrice = it->getContract().getStrike();
            // If the SPX finishes above the strike price, the value of the contract is the difference
            // Otherwise, it is worth nothing
            double intrinsicValue = (spxSettlementPrice > strikePrice) ? (spxSettlementPrice - strikePrice) : 0.0;

            // Calculate the cash settlement
            // (Negative quantities naturally result in cash being deducted)
            // And update the portfolio's bank account accordingly
            cash += it->getNumContracts() * intrinsicValue * 100.0;

            // Erase the dead contract
            // .erase() automatically returns a valid iterator pointing to the next element
            it = activeOptions.erase(it);
        }
        else
        {
            // The contract did not expire.
            // We manually step forward to check the next one
            ++it;
        }
    }
}

// RL Episode Management
// Reset method for future episodes
// Brings back the portfolio to its intial state
void Portfolio::reset()
{
    cash = initialCash;
    previousNAV = initialCash;
    spxPosition = SPXPosition();

    // Instantly wipes the active options array for the next episode
    activeOptions.clear();
}

// Returns the total profit/loss until this point
// Calculated by getting the difference between the current NAV and the initial cash
// Purely mathematical, does not alter the portfolio state
double Portfolio::getTotalPnL(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions) const
{
    return getNetAssetValue(currentSpxPrice, todaysCallOptions) - initialCash;
}

// Shows whether the AI outperforms the S&P 500
// Returns how much more or less the AI makes compared to just holding the SPX
// Calculated by getting the difference between the AI's current NAV
// and the value of the SPX if the AI bought as much it could at the beginning of the simulation
double Portfolio::getRelativePnL(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions) const
{
    // The double literal is the price of the S&P 500 at the beginning of the simulation
    return getTotalPnL(currentSpxPrice, todaysCallOptions) - (initialCash / 1132.990000 * currentSpxPrice - initialCash);
}

// Calculates the true usable buying power of the portfolio.
// In a margin account, when you short sell an asset (shares or options),
// the broker immediately credits your account with the cash proceeds of that sale.
// However, this cash is treated as a quarantined liability to ensure you can
// buy the asset back later; it is not free capital that can be used to open new positions.
// This function calculates the total liquid cash minus those quarantined short proceeds.
double Portfolio::getBuyingPower() const
{
    // Tracks the total amount of cash that was injected into the account via short selling
    double lockedProceeds = 0.0;

    // Lock cash from short SPX shares
    // If we are net-short on the S&P 500, we must quarantine the exact amount of cash
    // we received when we sold those borrowed shares
    if (spxPosition.getNumShares() < 0)
    {
        lockedProceeds += abs(spxPosition.getNumShares()) * spxPosition.getAvgCost();
    }

    // Lock cash from short Options
    // Iterate through all active option positions to find any naked or covered shorts
    for (const OptionPosition& optionPosition : activeOptions)
    {
        // A negative contract quantity indicates an open short position
        if (optionPosition.getNumContracts() < 0)
        {
            // Options are dealt in lots of 100. We lock the total premium received
            // when these contracts were initially written/sold.
            lockedProceeds += abs(optionPosition.getNumContracts()) * optionPosition.getAvgCost() * 100.0;
        }
    }

    // Usable buying power is the total cash balance minus the quarantined short proceeds
    // (Note: This strictly isolates cash. Initial/Maintenance margin requirements
    // are still enforced separately at the execution level in tradeOption)
    return cash - lockedProceeds;
}

// Getters
double Portfolio::getCash() const { return cash; }

const SPXPosition& Portfolio::getSpxPosition() const { return spxPosition; }

const vector<OptionPosition>& Portfolio::getActiveOptions() const { return activeOptions; }