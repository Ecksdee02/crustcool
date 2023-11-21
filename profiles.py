import uuid
import numpy as np
import subprocess
import os
import re


def get_profiles(profiles):

    for profile in profiles:
        source = 'out/prof'
        destination = 'out/prof_' + str(profile)
        name = str(uuid.uuid4())
        set_params(profile, name)
        subprocess.run(["./crustcool", name, '1'])
        os.system('rm /tmp/init.dat.' + name)
        os.system(f'cp {source} {destination}')
    return None


def set_params(profile,name):
	data="""resume 0

yHe		4
mass	1.62
radius	11.2
timetorun	10000.0
toutburst 2.5
mdot	0.1

precalc 0
ngrid	50
SFgap	1
kncrit	0
accreted 1

Qimp	0.6
Tc	6e7

# fixed top temperature during accretion:
cooling_bc	0
extra_heating	0
Tt	5.4e8

# or cooling boundary condition with shallow heating:
#cooling_bc	1
#extra_heating	1
#extra_Q	1.4
#extra_y	1e13
"""
	# Q,Lscale,Edep,Tc,M,R	
	#radius = ns.R(x[4],x[5])
	#Lmin = PYL(x[3]*1e7, 1e14, x[4], radius)
	
	params = {
		# 'Tc':x[0]*1e7,
		# 'Qimp':10.0**x[1],
#		'Lscale':x[1],
#		'Edep':x[2],
		# 'Tt':x[2]*1e8,
		'yHe':profile,
#		'mdot':x[3],
#		'mass':x[4],
#		'radius':radius,
#		'Lmin':Lmin
#		'extra_Q':x[0],
#		'extra_y':x[1]
	}

	for key,value in list(params.items()):
		data = re.sub("%s(\\t?\\w?).*\\n" % key,"%s\\t%g\\n" % (key,value),data)

	fout = open('/tmp/init.dat.'+name,'w')
	fout.write(data)
	fout.close()

profiles = np.array([4, 5, 6, 7, 8, 9])
get_profiles(profiles)