import unittest
from calibrate_fusion import fit, header


class CalibrationTests(unittest.TestCase):
    def test_affine_and_header(self):
        d=dict(pairs=[[x,y,.8*x+.1,.6*y+.2] for x,y in [(0,0),(1,0),(1,1),(0,1),(.5,.5)]],
               rgb_region=[.1,.1,.9,.9])
        a, _, error=fit(d)
        for actual, expected in zip(a,[.8,0,.1,0,.6,.2]):
            self.assertAlmostEqual(actual,expected)
        self.assertLess(error,1e-8)
        self.assertIn('FUSION_CALIBRATED=true',header(d)[0])

    def test_invalid_or_inconsistent_measurements_rejected(self):
        fixtures=[[[0,0,0,0]]*4,
                  [[0,0,0,0],[1,0,1,0],[1,1,0,0],[0,1,0,1]],
                  [[0,0,0,0],[1,0,1,0],[1,1,1,1],[0,1,float('nan'),1]]]
        for points in fixtures:
            with self.assertRaises(ValueError):fit(dict(pairs=points,rgb_region=[0,0,1,1]))


if __name__=='__main__':unittest.main()
