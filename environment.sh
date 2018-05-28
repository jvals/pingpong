SYSTEM=$1
case $SYSTEM in
idun)
	module load intel
	export CC=mpiicc
	;;
vilje)
	module load intelcomp/18.0.1 mpt/2.14
	export CC=mpicc
	;;
*)
	echo "Environment not predefined for system '${SYSTEM}'"
	;;
esac
