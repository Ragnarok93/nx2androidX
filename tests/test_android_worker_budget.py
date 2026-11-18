import unittest

from porter.resources import safe_jobs


class AndroidWorkerBudget(unittest.TestCase):
    def test_eight_gib_phone_uses_one_worker(self):
        self.assertEqual(safe_jobs(12, total_mb=7680, available_mb=5000, cpu_count=10, android=True), 1)

    def test_low_available_memory_reduces_worker_count(self):
        self.assertEqual(safe_jobs(8, total_mb=16384, available_mb=2500, cpu_count=8, android=True), 1)

    def test_twelve_gib_phone_is_limited_to_two_workers(self):
        self.assertEqual(safe_jobs(None, total_mb=11264, available_mb=6000, cpu_count=8, android=True), 2)

    def test_unknown_android_memory_fails_safe(self):
        self.assertEqual(safe_jobs(None, total_mb=0, available_mb=None, cpu_count=8, android=True), 1)

    def test_desktop_keeps_cpu_parallelism(self):
        self.assertEqual(safe_jobs(None, total_mb=8192, available_mb=3000, cpu_count=12, android=False), 12)

    def test_explicit_jobs_is_only_an_upper_bound(self):
        self.assertEqual(safe_jobs(1, total_mb=16384, available_mb=6000, cpu_count=8, android=True), 1)
