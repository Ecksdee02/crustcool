import numpy as np
import matplotlib.pyplot as plt

data = np.loadtxt("envelope_data/grid", dtype="float").transpose()
data_4 = np.loadtxt("envelope_data/grid_He4", dtype="float").transpose()
data_9 = np.loadtxt("envelope_data/grid_He9", dtype="float").transpose()
temp, flux = data[1], data[2]

def get_index(value):
    """ Get index of given flux """
    indices = []
    for i in range(len(flux)):
        if flux[i] == value:
            indices.append(i)
    return indices

def get_T(value):
    """ Return array of temperatures at given flux """
    indices = get_index(value)
    res = np.zeros(len(indices))
    for i, index in enumerate(indices):
        res[i] = temp[index]
    return res

def get_TF(helium):
    """ Return arrays of temperature and flux at given helium column depth """
    useful = np.array([elmt for elmt in data.transpose() if elmt[3] == helium]).transpose()
    return useful[1], useful[2]


def get_T_F4(y):
    useful  = np.array([elmt for elmt in data_4.transpose() if elmt[0] == y]).transpose()
    return useful[1], useful[2]

def get_T_F9(y):
    useful  = np.array([elmt for elmt in data_9.transpose() if elmt[0] == y]).transpose()
    return useful[1], useful[2]


""" Plot T vs yHe at different flux """
# yHe = np.arange(4.0, 9.1, 0.1)
# flux_values = np.arange(17, 20)
# 
# for f in flux_values:
#     plt.plot(yHe, get_T(f), label = "flux="+str(f))
# 
# plt.legend()
# plt.show()


""" Plot F vs T at different yHe """
yHe = np.arange(5, 10, 1)
for y in yHe:
    T, F = get_TF(y)
#     plt.plot(T, F, label = 'yHe = ' + str(y))

plt.plot(get_T_F9(10)[0], get_T_F9(10)[1], label = "original9")
plt.plot(get_T_F4(10)[0], get_T_F4(10)[1], ':', label = "original4")
plt.legend()
plt.show()

