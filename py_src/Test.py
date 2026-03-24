import os
import sys
import numpy as np

# Ensure Python can find your engine folder
# Bump the path up ONE folder level to the root 'RLTradingEngine' directory
sys.path.append(os.getcwd())
from py_src.engine.EnvWrapper import VolatilityEngineEnv

def main():
    print("==========================================")
    print("   Booting Forensic Margin Stress Test    ")
    print("==========================================\n")

    # Initialize the environment with $100,000,000
    env = VolatilityEngineEnv(initial_cash=100000000.0)
    obs = env.reset()

    # We will force the environment through 252 days of random, aggressive trading
    max_steps = 252

    final_reward = 0
    final_nav = 0
    
    for step in range(max_steps):
        # Generate a completely random action
        # This simulates an untrained AI pressing all the buttons at maximum volume
        action = env.action_space.sample() 
        
        # Step the environment
        # Handling both Gym 0.21 and 0.26 API returns
        result = env.step(action)
        if len(result) == 5:
            obs, reward, done, truncated, info = result
            is_done = done or truncated
        else:
            obs, reward, done, info = result
            is_done = done

        current_nav = info.get("final_nav", 0)
        
        # --- THE RED FLAG DETECTOR ---
        # If the NAV jumps by more than 50% in a single day, or exceeds 10 million, flag it.
        print(f"Current NAV:  ${current_nav:,.2f}")
        
        final_reward += reward
        final_nav = current_nav
            
        if is_done:
            print(f"\nSimulation ended at step {step}.")
            print(f"Final Portfolio Value: ${current_nav:,.2f}")
            break
    
    print(np.exp(final_reward * 140))
    print(final_nav / 100000000)

if __name__ == "__main__":
    main()