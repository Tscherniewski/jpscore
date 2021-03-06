#!/usr/bin/env python3

# Checks whether the pedestrian leaves the geometry via the designated entrance.
# Test for bug-fix.

import os
import sys

utestdir = os.path.abspath(os.path.dirname(os.path.dirname(sys.path[0])))
from sys import *

sys.path.append(utestdir)
from JPSRunTest import JPSRunTestDriver
from utils import *


def runtest10(inifile, trajfile):
    fps, N, traj = parse_file(trajfile)
    failure = False
    ids = np.unique(traj[:, 0]).astype(int)
    e1 = [90, -104., -102.7]  # x, y1, y2
    count1 = 0
    N = len(ids)

    for ped in ids:
        traj1 = traj[traj[:, 0] == ped]
        x = traj1[:, 2]
        y = traj1[:, 3]
        if PassedLineX(traj1, e1) != 0:
            count1 += 1
        else:
            logging.critical("ped %d did not pass one of the exits" % ped)
            failure = True

    logging.info(">> STATISTICS: Passed %d, Total %d <<" % (count1, N))

    if failure:
        logging.critical("%s exists with failure!" % argv[0])
        exit(FAILURE)


if __name__ == "__main__":
    test = JPSRunTestDriver(10, argv0=argv[0], testdir=sys.path[0], utestdir=utestdir)
    test.run_test(testfunction=runtest10)
    logging.info("%s exits with SUCCESS." % (argv[0]))
    exit(SUCCESS)
