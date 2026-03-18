#include <iostream>
#include <iomanip>
#include <vector>
#include "cpp_src/market/Market.h"
#include "cpp_src/portfolio/Portfolio.h"

using namespace std;

int main()
{
    cout << "=========================================\n";
    cout << " INITIALIZING QUANTITATIVE ENGINE...\n";
    cout << "=========================================\n\n";

    Market market;
    Portfolio portfolio(1000000.0); // $1M starting capital

    // 1. GET INITIAL MARKET STATE
    string initialDate = market.getTodaysDate();
    double initialSpxPrice = market.getCurrentSpxPrice();
    vector<const CallOption*> todaysMenu = market.getTodaysCallOptions();

    cout << "DATE: " << initialDate << " | SPX PRICE: $" << initialSpxPrice << "\n";
    cout << "AVAILABLE CONTRACTS: " << todaysMenu.size() << "\n\n";

    // 2. FORCE EXTREME TRADES (Buy one, Short another)
    // We grab two random contracts from the menu
    if (todaysMenu.size() > 100) 
    {
        const CallOption* contractToBuy = todaysMenu[10];   
        const CallOption* contractToShort = todaysMenu[50]; 

        cout << "--- EXECUTING TRADES ---\n";
        
        // Buy 10 contracts
        double buyPrice = contractToBuy->calculateMidPrice();
        bool buySuccess = portfolio.tradeOption(*contractToBuy, buyPrice, 10);
        cout << "BUY 10x Strike $" << contractToBuy->getStrike() 
             << " (Exp: " << contractToBuy->getExpirationDate() << ") @ $" 
             << buyPrice << " -> " << (buySuccess ? "SUCCESS" : "FAILED") << "\n";

        // Short 5 contracts
        double shortPrice = contractToShort->calculateMidPrice();
        bool shortSuccess = portfolio.tradeOption(*contractToShort, shortPrice, -5);
        cout << "SHORT 5x Strike $" << contractToShort->getStrike() 
             << " (Exp: " << contractToShort->getExpirationDate() << ") @ $" 
             << shortPrice << " -> " << (shortSuccess ? "SUCCESS" : "FAILED") << "\n\n";
    }

    // 3. EXECUTE THE MANDATORY DELTA HEDGE
    cout << "--- EXECUTING DELTA HEDGE ---\n";
    bool hedgeSuccess = portfolio.hedgeNetDelta(initialSpxPrice, todaysMenu);
    
    cout << "Hedge Execution -> " << (hedgeSuccess ? "SUCCESS" : "FAILED") << "\n";
    cout << "Current SPX Shares (Hedge): " << portfolio.getSpxPosition().getNumShares() << "\n";
    cout << "Current Liquid Cash: $" << fixed << setprecision(2) << portfolio.getCash() << "\n\n";

    // 4. SIMULATE TIME PASSAGE & EXPIRATIONS
    cout << "--- ADVANCING TIME (45 DAYS) ---\n";
    cout << "Watching Mark-to-Market NAV fluctuate...\n\n";
    
    // We step forward enough days to guarantee some options expire
    for (int i = 0; i < 45; i++) 
    {
        // Advance the clock
        market.endTradingDay();

        string newDate = market.getTodaysDate();
        double newSpxPrice = market.getCurrentSpxPrice();
        vector<const CallOption*> newMenu = market.getTodaysCallOptions();

        // Process any options that naturally expire today
        portfolio.processExpirations(newDate, newSpxPrice);

        // Print a weekly update to ensure Mark-to-Market and PnL are tracking correctly
        if (i % 7 == 0 || i == 44) 
        {
            double nav = portfolio.getNetAssetValue(newSpxPrice, newMenu);
            double sharpe =  portfolio.getRollingSharpeReward(newSpxPrice, newMenu);
            double totalPnL = portfolio.getTotalPnL(newSpxPrice, newMenu);

            cout << "Day " << i+1 << " (" << newDate << ") | SPX: $" << newSpxPrice 
                 << " | NAV: $" << nav 
                 << " | Sharpe Ratio: " << sharpe
                 << " | Total PnL: $" << totalPnL << "\n";
        }
    }

    cout << "\nActive Options Remaining in Portfolio: " << portfolio.getActiveOptions().size() << "\n";
    cout << "\n=========================================\n";
    cout << " ENGINE STRESS TEST COMPLETE.\n";
    cout << "=========================================\n";

    return 0;
}