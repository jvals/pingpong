import sys
import numpy as np
from matplotlib import pyplot as plt
from matplotlib import cm
import matplotlib.ticker as ticker

if(len(sys.argv) != 3):
    print("Usage: python3 plot.py input.csv delimiter")
    sys.exit(1)

dat = np.loadtxt(sys.argv[1], delimiter=sys.argv[2])

fig = plt.figure()
ax1 = fig.add_subplot(111) # https://matplotlib.org/api/_as_gen/matplotlib.figure.Figure.html

cmap = cm.get_cmap('Reds') # https://matplotlib.org/examples/color/colormaps_reference.html
                           # https://matplotlib.org/api/cm_api.html

ax1.imshow(dat, interpolation="none", cmap=cmap) # https://matplotlib.org/api/_as_gen/matplotlib.pyplot.imshow.html
# ax1.xaxis.set_major_locator(ticker.MultipleLocator(2))
plt.title("All to all pingpong latency")
plt.show()
