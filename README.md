# SPX Volatility Trading AI: C++ & Python Reinforcement Learning Engine

## Overview
This repository contains a Reinforcement Learning (RL) options trading environment, written in C++, designed to train a Proximal Policy Optimization (PPO) agent, written in Python, in the S&P 500 (SPX) options market. I built this project as a first time exploration rather rudimentary quantitative analysis and C++ development. The core engine, including a Black-Scholes Option Pricing model, is written entirely in C++. It's my first experience with the language. It is then bridged to Python via `pybind11` (it's pretty cool how that works) to interface with PyTorch and `Stable-Baselines3`.

This was my first time interactng with a market based on risk instead of betting on directional market movements. I have constructed it such that the environment applies strict risk management protocols that force the AI into a delta-neutral position. This requires the agent to generate alpha by trading the spread between implied volatility and historical volatility.

## Architecture
The project is decoupled into a high-speed C++ backend and a lightweight Python frontend, avoiding the performance bottlenecks typical of pure-Python financial simulations. Although, it's not like it made much difference since the AI was trained using a Raspberry Pi 400. However, here is the breakdown of the architecture:

* **C++ Backend (`rl_volatility_engine`)**: Handles 100% of the heavy lifting. It loads historical market data into RAM, calculates Black-Scholes theoretical pricing, tracks the portfolio Net Asset Value (NAV), processes expirations, and executes the math for the neural network's observation array.
* **Pybind11 Bridge (`Bindings.cpp`)**: Drops the Python Global Interpreter Lock (GIL) during C++ execution, allowing true parallel processing across multiple CPU cores.
* **Python Frontend (`EnvWrapper.py` & `Train.py`)**: A Gym-compliant wrapper that feeds the C++ state arrays directly into the Stable-Baselines3 PPO algorithm. It uses a custom `ThreadVecEnv` to run multiple environment instances concurrently on different threads.

This architecture didn't always use to look like this. Initially I actually tried paging the data, instead of reading all of the data into RAM. However, that proved to be significantly slower than just reading all of the data. Therefore, I decided to go with loading everything into RAM at once. There was still an issue though. With the way memory allocation works between Python and C++, it did not matter that the call option data would be loaded as a static array, whenever I tried to multithread the program, it would create copies of the array, and therefore overshoot the RAM limitations. This created the need for the custom `ThreadVecEnv` functionality, and solved the issue of being able to multithread the program whilst keeping RAM usage to under 4GB.

## Environment Mechanics

### State Space (Observation)
The AI receives a continuous **796-element array** (`Box(-inf, inf, shape=(796,))`) every trading day. Because the options chain changes daily, the environment maps the available contracts onto a fixed **9x11 spatial grid** based on two dimensions: *Days-to-Expiration* (x-axis) and *Moneyness* (y-axis). 

For each of the 99 grid slots, the AI sees 8 features:
1. **Exact Moneyness:** (K / S)
2. **Exact DTE:** Days to expiration.
3. **Edge Ratio:** Theoretical Black-Scholes Price vs. Actual Market Mid-Price.
4. **Delta:** Directional risk.
5. **Gamma:** Rate of change of Delta.
6. **Theta:** Time decay.
7. **Rho:** Interest rate sensitivity.
8. **Spread Percentage:** Liquidity cost metric.

Additionally, 4 global macroeconomic features are appended: 
1. 30-Day Historical Volatility of the SPX.
2. 3-Month Risk-Free Treasury Rate.
3. Portfolio Net Delta.
4. Available Buying Power Percentage.

### Action Space
The AI outputs a **99-element array** (`Box(-1.0, 1.0, shape=(99,))`). 
Each value represents the percentage of available buying power the AI wishes to allocate to a specific contract on the grid. 
* `-1.0` = Maximum Short Allocation
* `0.0` = Flat (Hold / Do Nothing)
* `1.0` = Maximum Long Allocation

### Reward Function: Scaled Logarithmic Returns
To prevent the PPO algorithm from falling into the "Entropy Trap" or exploiting rolling-average summation bugs (like farming low-volatility flat returns), the AI is rewarded using strictly scaled daily logarithmic returns:

$$R_t = \ln\left(\frac{NAV_t}{NAV_{t-1}}\right) \times 100$$

By multiplying the log return by 100, the gradients are scaled for PyTorch's loss calculations, overpowering the entropy coefficient and forcing the Critic network to prioritize compound capital growth. A massive terminal penalty is applied if the portfolio drops below the bankruptcy threshold, letting the math naturally teach the AI the cost of bankrupcy.

## Financial Data & Assumptions
The environment simulates historical trading using **14 years of SPX End-of-Day (EOD) call option data** (2010–2024), paired with daily U.S. Treasury Yield Curve rates.

**Key Engine Rules & Assumptions:**
1. **The Black-Scholes Engine:** The C++ `CallOption` class calculates the theoretical price dynamically using the 30-day historical SPX volatility and the linearly interpolated risk-free rate for the specific contract's DTE. 
2. **Intrinsic Value Floor:** To prevent the AI from exploiting $0.00 ghost liquidity on deep out-of-the-money options, all options are mathematically clamped to never fall below their intrinsic value (`max(mid_price, S - K)`).
3. **Mandatory Hedging:** At the end of every trading step, the `Portfolio::hedgeNetDelta()` function fires. It buys or shorts underlying SPX shares to bring the portfolio's total Delta back to zero.
4. **Frictions:** Transaction costs are hardcoded into the execution logic ($1.50 per options contract, $0.005 per SPX share). Short-sale proceeds are quarantined and cannot be used as buying power.
5. **EOD Slippage (Market Friction):** Because the simulation relies on End-of-Day (EOD) data, synthetic market friction is applied. The AI is forced to execute trades at a strict 20% penalty (buying at 20% above and selling at 20% below the fair mid-price) to simulate realistic bid-ask spreads, execution slippage, and the impossibility of perfectly capturing EOD marks.
6. **Horizon Bias Prevention:** The random spawn point for an episode is clamped so the AI is mathematically guaranteed to always have a full 2,016-day (8-year) runway, preventing the Critic network from getting confused by truncated datasets.

## Training Custom Metrics
Because the standard `ep_rew_mean` tracks the gamified sum of the neural gradients, real financial performance is intercepted by a custom `MetricsTrackerCallback`. This callback logs the unadulterated `custom/avg_final_portfolio_value` directly to TensorBoard exactly once at the end of every episode, preventing diluted rolling averages.

## Results
As of the last update, the AI is still training. However, there is evidence of the AI improving and learning.
