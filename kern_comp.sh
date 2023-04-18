make ARCH=um
make modules ARCH=um
make modules_install ARCH=um
gnome-terminal -- bash -c './linux mem=2048M umid=TEST ima_policy=tcb ubd0=fs_16 vec0:transport=tap,ifname=tap0,depth=128,gro=1 root=/dev/ubda con=null con0=null,fd:2 con1=fd:0,fd:1'
