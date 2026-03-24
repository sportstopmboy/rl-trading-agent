import numpy as np
import concurrent.futures
from stable_baselines3.common.vec_env import VecEnv

class ThreadVecEnv(VecEnv):
    """
    Custom Vectorized Environment that uses Threads instead of OS Processes.
    This allows multiple environments to execute C++ code simultaneously on multiple CPU cores
    while sharing the exact same static RAM block in the master Python process.
    """
    def __init__(self, env_fns):
        # Initialize all the environments from the provided functions
        self.envs = [fn() for fn in env_fns]
        env = self.envs[0]
        
        # Initialize the Stable Baselines3 Base Class
        super().__init__(len(env_fns), env.observation_space, env.action_space)
        
        # Create a Thread Pool with one worker per environment
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=len(env_fns))
        self.actions = None

    def step_async(self, actions):
        # SB3 calls this to tell the envs what actions to take
        self.actions = actions

    def step_wait(self):
        # Submit all steps to the thread pool simultaneously
        futures = [self.executor.submit(env.step, action) for env, action in zip(self.envs, self.actions)]
        results = [f.result() for f in futures]
        
        # Unpack the results returned by your EnvWrapper (Gymnasium 0.26 API)
        obs_list, rews_list, terms_list, truncs_list, infos_list = zip(*results)
        
        # SB3 VecEnv expects a unified "done" boolean array
        dones_list = [term or trunc for term, trunc in zip(terms_list, truncs_list)]
        
        # Convert tuples to mutable lists so we can update them during resets
        obs_list = list(obs_list)
        infos_list = list(infos_list)
        
        # Mandatory SB3 Logic: If an environment finishes, it MUST auto-reset instantly
        for i in range(self.num_envs):
            if dones_list[i]:
                # Save the final observation before the reset into the info dict
                infos_list[i]["terminal_observation"] = obs_list[i]
                
                # Execute the reset and replace the observation with the fresh Day-1 state
                obs_list[i], reset_info = self.envs[i].reset()
                infos_list[i].update(reset_info)
        
        # Return the stacked numpy arrays back to the PPO neural network
        return np.stack(obs_list), np.stack(rews_list), np.stack(dones_list), infos_list

    def reset(self):
        # Submit all resets to the thread pool simultaneously
        futures = [self.executor.submit(env.reset) for env in self.envs]
        results = [f.result() for f in futures]
        obs_list = [r[0] for r in results]
        return np.stack(obs_list)

    def close(self):
        # Safely shut down the thread pool and environments
        self.executor.shutdown(wait=True)
        for env in self.envs:
            if hasattr(env, 'close'):
                env.close()

    # Required boilerplate methods for SB3 VecEnv compatibility
    def get_attr(self, attr_name, indices=None):
        return [getattr(env, attr_name) for env in self.envs]

    def set_attr(self, attr_name, value, indices=None):
        for env in self.envs: setattr(env, attr_name, value)

    def env_method(self, method_name, *args, indices=None, **kwargs):
        return [getattr(env, method_name)(*args, **kwargs) for env in self.envs]

    def env_is_wrapped(self, wrapper_class, indices=None):
        return [False] * self.num_envs

    def seed(self, seed=None):
        return [None] * self.num_envs