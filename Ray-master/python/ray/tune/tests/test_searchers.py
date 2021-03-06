import unittest

import numpy as np

import ray
from ray import tune


def _invalid_objective(config):
    # DragonFly uses `point`
    metric = "point" if "point" in config else "report"

    if config[metric] > 4:
        tune.report(float("inf"))
    elif config[metric] > 3:
        tune.report(float("-inf"))
    elif config[metric] > 2:
        tune.report(np.nan)
    else:
        tune.report(float(config[metric]) or 0.1)


class InvalidValuesTest(unittest.TestCase):
    """
    Test searcher handling of invalid values (NaN, -inf, inf).
    Implicitly tests automatic config conversion and default (anonymous)
    mode handling.
    """

    def setUp(self):
        self.config = {"report": tune.uniform(0.0, 5.0)}

    def tearDown(self):
        pass

    @classmethod
    def setUpClass(cls):
        ray.init(num_cpus=4, num_gpus=0, include_dashboard=False)

    @classmethod
    def tearDownClass(cls):
        ray.shutdown()

    def testAx(self):
        from ray.tune.suggest.ax import AxSearch
        from ax.service.ax_client import AxClient

        converted_config = AxSearch.convert_search_space(self.config)
        # At least one nan, inf, -inf and float
        client = AxClient(random_seed=4321)
        client.create_experiment(
            parameters=converted_config,
            objective_name="_metric",
            minimize=False)
        searcher = AxSearch(ax_client=client)

        out = tune.run(
            _invalid_objective,
            search_alg=searcher,
            metric="_metric",
            mode="max",
            num_samples=4,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testBayesOpt(self):
        from ray.tune.suggest.bayesopt import BayesOptSearch

        out = tune.run(
            _invalid_objective,
            # At least one nan, inf, -inf and float
            search_alg=BayesOptSearch(random_state=1234),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testBlendSearch(self):
        from ray.tune.suggest.flaml import BlendSearch

        out = tune.run(
            _invalid_objective,
            search_alg=BlendSearch(),
            config=self.config,
            metric="_metric",
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testBOHB(self):
        from ray.tune.suggest.bohb import TuneBOHB

        out = tune.run(
            _invalid_objective,
            search_alg=TuneBOHB(seed=1000),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testCFO(self):
        from ray.tune.suggest.flaml import CFO

        out = tune.run(
            _invalid_objective,
            search_alg=CFO(),
            config=self.config,
            metric="_metric",
            mode="max",
            num_samples=16,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testDragonfly(self):
        from ray.tune.suggest.dragonfly import DragonflySearch

        np.random.seed(1000)  # At least one nan, inf, -inf and float

        out = tune.run(
            _invalid_objective,
            search_alg=DragonflySearch(domain="euclidean", optimizer="random"),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["point"], 2.0)

    def testHEBO(self):
        from ray.tune.suggest.hebo import HEBOSearch

        out = tune.run(
            _invalid_objective,
            # At least one nan, inf, -inf and float
            search_alg=HEBOSearch(random_state_seed=123),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testHyperopt(self):
        from ray.tune.suggest.hyperopt import HyperOptSearch

        out = tune.run(
            _invalid_objective,
            # At least one nan, inf, -inf and float
            search_alg=HyperOptSearch(random_state_seed=1234),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testNevergrad(self):
        from ray.tune.suggest.nevergrad import NevergradSearch
        import nevergrad as ng

        np.random.seed(2020)  # At least one nan, inf, -inf and float

        out = tune.run(
            _invalid_objective,
            search_alg=NevergradSearch(optimizer=ng.optimizers.RandomSearch),
            config=self.config,
            mode="max",
            num_samples=16,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testOptuna(self):
        from ray.tune.suggest.optuna import OptunaSearch
        from optuna.samplers import RandomSampler

        np.random.seed(1000)  # At least one nan, inf, -inf and float

        out = tune.run(
            _invalid_objective,
            search_alg=OptunaSearch(sampler=RandomSampler(seed=1234)),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testSkopt(self):
        from ray.tune.suggest.skopt import SkOptSearch

        np.random.seed(1234)  # At least one nan, inf, -inf and float

        out = tune.run(
            _invalid_objective,
            search_alg=SkOptSearch(),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)

    def testZOOpt(self):
        self.skipTest(
            "Recent ZOOpt versions fail handling invalid values gracefully. "
            "Skipping until we or they found a workaround. ")
        from ray.tune.suggest.zoopt import ZOOptSearch

        np.random.seed(1000)  # At least one nan, inf, -inf and float

        out = tune.run(
            _invalid_objective,
            search_alg=ZOOptSearch(budget=100, parallel_num=4),
            config=self.config,
            mode="max",
            num_samples=8,
            reuse_actors=False)

        best_trial = out.best_trial
        self.assertLessEqual(best_trial.config["report"], 2.0)


class AddEvaluatedPointTest(unittest.TestCase):
    """
    Test add_evaluated_point method in searchers that support it.
    """

    def setUp(self):
        self.param_name = "report"
        self.valid_value = 1.0
        self.space = {self.param_name: tune.uniform(0.0, 5.0)}

    def tearDown(self):
        pass

    @classmethod
    def setUpClass(cls):
        ray.init(num_cpus=4, num_gpus=0, include_dashboard=False)

    @classmethod
    def tearDownClass(cls):
        ray.shutdown()

    def testOptuna(self):
        from ray.tune.suggest.optuna import OptunaSearch
        from optuna.trial import TrialState

        searcher = OptunaSearch(
            space=self.space,
            metric="metric",
            mode="max",
            points_to_evaluate=[{
                self.param_name: self.valid_value
            }],
            evaluated_rewards=[1.0])

        self.assertGreater(len(searcher._ot_study.trials), 0)

        searcher = OptunaSearch(
            space=self.space,
            metric="metric",
            mode="max",
        )

        point = {
            self.param_name: self.valid_value,
        }

        self.assertEqual(len(searcher._ot_study.trials), 0)

        searcher.add_evaluated_point(
            point, 1.0, intermediate_values=[0.8, 0.9])
        self.assertEqual(len(searcher._ot_study.trials), 1)
        self.assertTrue(
            searcher._ot_study.trials[-1].state == TrialState.COMPLETE)

        searcher.add_evaluated_point(
            point, 1.0, intermediate_values=[0.8, 0.9], error=True)
        self.assertEqual(len(searcher._ot_study.trials), 2)
        self.assertTrue(searcher._ot_study.trials[-1].state == TrialState.FAIL)

        searcher.add_evaluated_point(
            point, 1.0, intermediate_values=[0.8, 0.9], pruned=True)
        self.assertEqual(len(searcher._ot_study.trials), 3)
        self.assertTrue(
            searcher._ot_study.trials[-1].state == TrialState.PRUNED)

    def testHEBO(self):
        from ray.tune.suggest.hebo import HEBOSearch

        searcher = HEBOSearch(
            space=self.space,
            metric="metric",
            mode="max",
        )

        point = {
            self.param_name: self.valid_value,
        }

        len_X = len(searcher._opt.X)
        len_y = len(searcher._opt.y)
        self.assertEqual(len_X, 0)
        self.assertEqual(len_y, 0)

        searcher.add_evaluated_point(point, 1.0)

        len_X = len(searcher._opt.X)
        len_y = len(searcher._opt.y)
        self.assertEqual(len_X, 1)
        self.assertEqual(len_y, 1)


if __name__ == "__main__":
    import pytest
    import sys
    sys.exit(pytest.main(["-v", __file__]))
