import argparse
import os
import sys
import numpy as np
from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import DummyVecEnv
from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback
from stable_baselines3.common.monitor import Monitor

# Get the absolute path of the current directory (RLTradingEngine)
# and add it to the system path
sys.path.append(os.getcwd())

# The Custom Metric Interceptor
# This allows us to track portfolio value on top of the other metrics
class MetricsTrackerCallback(BaseCallback):
    def __init__(self, verbose=0):
        super().__init__(verbose)
        self.final_navs = []
        self.episode_sharpes = []

    def _on_step(self) -> bool:
        # Check the info dictionary for every step
        for info in self.locals.get("infos", []):
            # Track the NAV (Once we add it to the C++ struct)
            if "final_nav" in info:
                self.final_navs.append(info["final_nav"])
            
            # Track the Episode Reward and Length to calculate Average Sharpe
            # The 'Monitor' wrapper automatically injects an 'episode' dict when an episode ends
            if "episode" in info:
                ep_reward = info["episode"]["r"]
                ep_length = info["episode"]["l"]
                
                # Calculate: (reward + 10) / episode length
                # Protect against division by zero
                if ep_length > 0:
                    daily_sharpe = (ep_reward + 10.0) / ep_length
                    self.episode_sharpes.append(daily_sharpe)
                    
        return True

    def _on_rollout_end(self) -> None:
        # Every time PPO stops to update its brain, average the metrics and log them
        if len(self.final_navs) > 0:
            avg_nav = np.mean(self.final_navs)
            self.logger.record("custom/avg_final_portfolio_value", avg_nav)
            self.final_navs = [] 

        if len(self.episode_sharpes) > 0:
            avg_sharpe = np.mean(self.episode_sharpes)
            self.logger.record("custom/avg_daily_sharpe", avg_sharpe)
            self.episode_sharpes = []

# Ensure Python can find your engine folder
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from py_src.engine.EnvWrapper import VolatilityEngineEnv

def main():
    print("==========================================")
    print("    Booting Volatility Engine Training    ")
    print("==========================================\n")

    # Command Line Argument Parser
    # This accepts the amount of steps the AI is going to take before completing traning
    parser = argparse.ArgumentParser(description="Train the PPO Volatility Trading Bot")
    parser.add_argument("--steps", type=int, default=1000000, help="Total timesteps to train the AI")
    # Allow loading a saved model
    parser.add_argument("--load", type=str, default=None, help="Path to a saved .zip model to resume training")
    # Allow naming the TensorBoard run so it appends to the same folder
    parser.add_argument("--run", type=str, default="PPO_Run", help="Name of the TensorBoard folder to log to")
    args = parser.parse_args()
    
    # Store the parsed number into a variable
    TOTAL_TIMESTEPS = args.steps
    
    # Setup Directories for Logging and Model Weights
    models_dir = "build/models/PPO"
    logdir = "build/logs"
    os.makedirs(models_dir, exist_ok=True)
    os.makedirs(logdir, exist_ok=True)

    # Instantiate the Environment
    print("Initializing the C++ Backend...")
    
    # Create a function that wraps your environment with the Monitor clipboard
    def make_env():
        base_env = VolatilityEngineEnv(initial_cash=1000000.0)
        return Monitor(base_env) # <--- This records the rewards

    # Pass it into the Vectorized Environment
    env = DummyVecEnv([make_env])
    print("Successfully initialized the C++ Backend.\n")

    # Define the PPO Neural Network (The Brain)
    reset_timesteps = True # Default behavior for a new model

    if args.load is not None:
        print(f"Loading existing brain from: {args.load}...")
        # Load the model and attach the fresh environment to it
        model = PPO.load(args.load, env=env)
        # Tell TensorBoard NOT to reset the x-axis to 0
        reset_timesteps = False 
        print("Successfully loaded model. Resuming training...\n")
    else:
        print("Initializing new PPO Neural Network...")
        model = PPO(
            policy="MlpPolicy",         # Standard Multi-Layer Perceptron (Feed-forward Neural Network)
            env=env,
            verbose=1,                  # Prints basic progress to the terminal
            tensorboard_log=logdir,     # Wires the brain into TensorBoard
            learning_rate=0.0003,       # Standard starting learning rate
            n_steps=2016,               # How many trades to execute before updating the brain 
                                        # (2016 is 8 years worth of trading days)
            batch_size=1008,            # How many trades to analyze at once during an update
                                        # (1008 is 4 years worth of trading days)
            ent_coef=0.01,              # Entropy coefficient - forces the AI to explore new strategies
        )
        print("Successfully initialized PPO Neural Network.\n")

    # Create a Checkpoint Callback
    # If the program crashes or power goes out on step 400,000, you don't lose your progress
    # This saves the model weights every 10,000 trades
    checkpoint_callback = CheckpointCallback(
        save_freq=10000,
        save_path=models_dir,
        name_prefix="ppo_volatility_bot"
    )

    # Instantiate the custom tracker
    metrics_tracker = MetricsTrackerCallback()

    # Train the AI
    print(f"Starting {TOTAL_TIMESTEPS:,} step training run...")
    print("Open a new terminal and run 'py -m tensorboard.main --logdir=build/logs --bind_all' to watch it learn.\n")
    
    # Pass the variable into the learn function
    model.learn(
        total_timesteps=TOTAL_TIMESTEPS, 
        callback=[checkpoint_callback, metrics_tracker], 
        tb_log_name=args.run,
        reset_num_timesteps=reset_timesteps # Ensures charts stay continuous
    )

    # Save the final finalized model
    print("\n========================================")
    print("Training complete! Saving final model...")
    print("==========================================\n")
    model.save(f"{models_dir}/ppo_volatility_bot_final")
    print("Model saved.\n")

if __name__ == "__main__":
    main()