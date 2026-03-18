import gymnasium as gym
from gymnasium import spaces
import numpy as np

# Since Train.py already added 'py_src' to the path, Python knows 'engine' is a package
# This allows us to import the engine directly
from engine import rl_volatility_engine 

# Define the Environment withing Python
class VolatilityEngineEnv(gym.Env):
    def __init__(self, initial_cash=1000000.0):
        super(VolatilityEngineEnv, self).__init__()

        # Initialize the C++ backend
        self.cpp_env = rl_volatility_engine.Environment(initial_cash)

        # Define Action Space: 99 continuous values between -1.0 and 1.0
        # -1.0 = Max Short, 0.0 = Flat, 1.0 = Max Long
        self.action_space = spaces.Box(low=-1.0, high=1.0, shape=(99,), dtype=np.float32)

        # Define Observation Space: 796 continuous features
        # We use large bounds because raw Greeks and Vol can be high
        self.observation_space = spaces.Box(low=-np.inf, high=np.inf, shape=(796,), dtype=np.float32)

        # Track the starting cash for Python math
        self.initial_cash = initial_cash
        self.current_nav = initial_cash
    
    # Define the reset function within Python
    def reset(self, seed=None, options=None):
        # Mandatory: Seed the RNG if needed
        super().reset(seed=seed)

        # Reset the NAV tracker
        self.current_nav = self.initial_cash
        
        # Call C++ reset
        raw_obs = self.cpp_env.reset()
        
        # Convert C++ list to a NumPy array for PyTorch
        obs = np.array(raw_obs, dtype=np.float32)
        
        return obs, {}
    
    def step(self, action):
        # Pass the array to the C++ engine
        result = self.cpp_env.step(action.tolist())
        
        obs = np.array(result.state_features, dtype=np.float32)
        reward = result.reward
        terminated = result.is_done
        truncated = False
        
        info = {}
        
        # Pass it to Stable-Baselines3 so the custom callback can chart it.
        info['final_nav'] = result.current_nav 

        return obs, reward, terminated, truncated, info