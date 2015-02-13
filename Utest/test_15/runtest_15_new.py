#!/usr/bin/env python
import os
import sys
from sys import *
import scipy
import scipy.stats
import matplotlib.pyplot as plt
sys.path.append(os.path.abspath(os.path.dirname(sys.path[0])))
from JPSRunTest import JPSRunTestDriver
from utils import *

__author__ = 'Oliver Schmidts'


def runtest15(inifile, trajfile):
    logsim = "inifiles/log.P0.dat"
    if not path.exists(logsim):
        logging.critical("logsim <%s> does not exist"%logsim)
        exit(FAILURE)
    logging.info("open  <%s> "%logsim)
    f = open(logsim, "r")
    for line in f:
        if line.startswith("Exec"):
            exec_time = float( line.split()[-1])

        if line.startswith("Evac"):
            evac_time = float(line.split()[-1])

    f.close()


    if evac_time < exec_time:
        logging.info("%s exits with FAILURE evac_time = %f, exec_time = %f)"%(argv[0], evac_time, exec_time))
        exit(FAILURE)
    else:
        logging.info("evac_time = %f  exec_time = %f)"%(evac_time, exec_time))


if __name__ == "__main__":
    test = JPSRunTestDriver(15, argv0=argv[0], testdir=sys.path[0])
    test.run_test(runtest15)
    logging.info("%s exits with SUCCESS"%(argv[0]))
    exit(SUCCESS)