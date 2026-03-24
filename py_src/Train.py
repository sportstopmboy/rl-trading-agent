import argparse
import os
import sys

from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback
from stable_baselines3.common.monitor import Monitor

# Get the absolute path of the root directory (RLTradingEngine) and add it to the system path
sys.path.append(os.getcwd())

# Import your custom modules from the engine folder
from py_src.engine.EnvWrapper import VolatilityEngineEnv
from py_src.engine.ThreadVecEnv import ThreadVecEnv
from py_src.engine.MetricsTrackerCallback import MetricsTrackerCallback

def main():
    print("==========================================")
    print("    Booting Volatility Engine Training    ")
    print("==========================================\n")

    # Command Line Argument Parser
    parser = argparse.ArgumentParser(description="Train the PPO Volatility Trading Bot")
    parser.add_argument("--steps", type=int, default=1000000, help="Total timesteps to train the AI")
    parser.add_argument("--load", type=str, default=None, help="Path to a saved .zip model to resume training")
    parser.add_argument("--run", type=str, default="PPO_Run", help="Name of the TensorBoard folder to log to")
    args = parser.parse_args()
    
    TOTAL_TIMESTEPS = args.steps
    
    # Setup Directories for Logging and Model Weights
    models_dir = "build/models/PPO"
    logdir = "build/logs"
    os.makedirs(models_dir, exist_ok=True)
    os.makedirs(logdir, exist_ok=True)

    print("Initializing the C++ Backend...")
    
    # Create a function that wraps your environment with the Monitor clipboard
    def make_env():
        # Using $100 Million institutional capital for proper SPX hedging
        base_env = VolatilityEngineEnv(initial_cash=100000000.0)
        return Monitor(base_env)

    # Determine how many cores to use
    num_envs = 4
    env_fns = [make_env for _ in range(num_envs)]
    
    # Load them into your custom Threaded Vectorized Environment
    env = ThreadVecEnv(env_fns)
    print(f"Successfully initialized {num_envs} Parallel Environments.\n")

    # Define the PPO Neural Network
    reset_timesteps = True 

    if args.load is not None:
        print(f"Loading existing brain from: {args.load}...")
        model = PPO.load(args.load, env=env)
        reset_timesteps = False 
        print("Successfully loaded model. Resuming training...\n")
    else:
        print("Initializing new PPO Neural Network...")
        model = PPO(
            policy="MlpPolicy",         
            env=env,
            verbose=1,                  
            tensorboard_log=logdir,     
            learning_rate=0.0003,       
            n_steps=2016,               # 4 envs * 2016 = 8,064 trades per update       
            batch_size=1008,            # 8064 / 1008 = 8 perfectly even batches
            ent_coef=0.01,              
        )
        print("Successfully initialized PPO Neural Network.\n")

    # Create Callbacks
    checkpoint_callback = CheckpointCallback(
        save_freq=10000,
        save_path=models_dir,
        name_prefix="ppo_volatility_bot"
    )

    metrics_tracker = MetricsTrackerCallback()

    # Train the AI
    print(f"Starting {TOTAL_TIMESTEPS:,} step training run on {num_envs} threads...")
    print("Open a new terminal and run 'tensorboard --logdir=build/logs --bind_all' to watch it learn.\n")
    
    model.learn(
        total_timesteps=TOTAL_TIMESTEPS, 
        callback=[checkpoint_callback, metrics_tracker], 
        tb_log_name=args.run,
        reset_num_timesteps=reset_timesteps 
    )

    # Save the final model
    print("\n========================================")
    print("Training complete! Saving final model...")
    print("==========================================\n")
    model.save(f"{models_dir}/ppo_volatility_bot_final")
    print("Model saved.\n")

if __name__ == "__main__":
    main()