SUBDIRS = common api db_api database \
	slurmctld slurmd slurmdbd plugins sbcast \
	scontrol scancel squeue sinfo smap sview salloc \
	sbatch sattach strigger sacct sacctmgr sreport sstat \
	sshare sprio sdiag srun_cr

if !REAL_BG_L_P_LOADED
SUBDIRS += srun
endif
