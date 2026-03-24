import numpy as np
from stable_baselines3.common.callbacks import BaseCallback

class MetricsTrackerCallback(BaseCallback):
    """
    Custom Metric Interceptor for Stable Baselines3.
    This callback hooks into the PPO training loop to extract and log 
    custom financial metrics (like NAV and Episode Length) to TensorBoard.
    """
    def __init__(self, verbose=0):
        super().__init__(verbose)
        self.final_navs = []

    def _on_step(self) -> bool:
        # Check the info dictionary for every step across all parallel environments
        for info in self.locals.get("infos", []):
            
            # The 'Monitor' wrapper automatically injects an 'episode' dict 
            # ONLY on the exact step when an environment terminates or truncates.
            if "episode" in info:
                # 2. Track the Final NAV
                # Because we are inside the "episode ended" block, this guarantees 
                # we only grab the NAV from the very last day of the run.
                if "final_nav" in info:
                    self.final_navs.append(info["final_nav"])
                    
        return True

    def _on_rollout_end(self) -> None:
        # Average the metrics and push them to TensorBoard
        if len(self.final_navs) > 0:
            avg_nav = np.mean(self.final_navs)
            self.logger.record("custom/avg_final_portfolio_value", avg_nav)
            self.final_navs = []