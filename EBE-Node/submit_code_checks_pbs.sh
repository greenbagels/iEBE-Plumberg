#!/bin/bash

baseDirectory=$HOME/Plumberg_iEBE/iEBE-stable/EBE-Node
homeDirectory=$baseDirectory/code_checks
outfilename=$homeDirectory/"submit_jobs_record_`date +%F`.out"
jobIDsfilename=$homeDirectory/"jobIDs_`date +%F`.out"
jobIDsfile=`get_filename $jobIDsfilename`
outfile=`get_filename $outfilename`
srcDirec=$baseDirectory/HoTCoffeeh

i=1
workingDirectory=$HOME'/Plumberg_iEBE/iEBE-stable/RESULTS_Edec300/results/results-'`echo $i`
#workingDirectory=$HOME'/Plumberg_iEBE/iEBE-stable/avgRESULTS/job-1/event-1'

npt0=15
npphi0=36
npy0=15

#phase space size to run
ps0=$1
mv $srcDirec/cfwr.e $srcDirec/cfwr_`echo $ps0`.e

#run at most this many jobs at a time
nMaxProcessesRunning=16

declare -A qxSizes=( ["XYZ"]=7 ["X"]=51 ["Y"]=1 ["Z"]=1)
declare -A qySizes=( ["XYZ"]=7 ["X"]=1 ["Y"]=51 ["Z"]=1)
declare -A qzSizes=( ["XYZ"]=7 ["X"]=1 ["Y"]=1 ["Z"]=51)
declare -A dqxVals=( ["XYZ"]=0.025 ["X"]=0.003 ["Y"]=0.001 ["Z"]=0.001)
declare -A dqyVals=( ["XYZ"]=0.025 ["X"]=0.001 ["Y"]=0.003 ["Z"]=0.001)
declare -A dqzVals=( ["XYZ"]=0.020 ["X"]=0.001 ["Y"]=0.001 ["Z"]=0.003)

#submit jobs
for axis in XYZ
do
	for nqt0 in 17 23 31
	do
		for npy0 in 15 21
		do
			###########
			direcName0=$homeDirectory/AXIS_`echo $axis`_qt`echo $nqt0`_pT`echo $npt0`_pY`echo $npy0`_PS`echo $ps0`
			if [ ! -d "$direcName0" ]
			then
				mkdir $direcName0
			fi
			###########
			nqx0=${qxSizes[$axis]}
			nqy0=${qySizes[$axis]}
			nqz0=${qzSizes[$axis]}
			dqx=${dqxVals[$axis]}
			dqy=${dqyVals[$axis]}
			dqz=${dqzVals[$axis]}

			for resfrac in 0.00 0.10 0.20 0.30 0.40 0.50 0.60 0.70 0.80 0.90 1.00
			do
				##########################################
				# before submitting any more jobs, make sure you aren't at the max. limit
				##########################################
				echo 'Waiting to generate next submission...' >> $outfile
				nProcessesRunning=`qstat -u plumberg.1 | grep plumberg.1 | awk '$(NF-1)=="R"' | wc -l`
				until [ "$nProcessesRunning" -lt "$nMaxProcessesRunning" ]
				do
					#echo $nProcessesRunning '==' $nMaxProcessesRunning "processes currently running at" `date` >> $outfile
					sleep 10
					nProcessesRunning=`qstat -u plumberg.1 | grep plumberg.1 | awk '$(NF-1)=="R"' | wc -l`
				done
				echo $nProcessesRunning '<' $nMaxProcessesRunning "processes currently running at" `date` >> $outfile
				echo 'Generating next submission...' >> $outfile
				##########################################

				###########
				direcName=$direcName0/RESFRAC_`echo $resfrac`
				if [ -d "$direcName" ]
				then
					continue
					#rm -rf $direcName
				fi
				mkdir $direcName
				###########

				lwd=$direcName0/RESFRAC_`echo $resfrac`
				mkdir $lwd/results

				cp $workingDirectory/decdat2.dat $workingDirectory/decdat_mu.dat $workingDirectory/surface.dat $lwd/results

				newPBSscriptName=run_cfwr_NEWEVENTS.pbs
				cp run_cfwr_NEWEVENTS.pbs $lwd/$newPBSscriptName
				(
					cd $lwd

					\cp $srcDirec/cfwr_`echo $ps0`.e ./cfwr.e
					\cp $srcDirec/parameters.dat .
					\cp -r $srcDirec/EOS .

					echo 'Results directory and submission ID:' \
							`qsub -v workingDirectory=$lwd,NPT=$npt0,NPPHI=$npphi0,NPY=$npy0,NQT=$nqt0,NQX=$nqx0,NQY=$nqy0,NQZ=$nqz0,DQX=$dqx,DQY=$dqy,DQZ=$dqz,RESFRAC=$resfrac $newPBSscriptName` >> $outfile
					cd ..;
					echo 'Submitted at' `date` >> $outfile
					sleep 3
					#update list of currently running jobs in case I need to kill all currently running jobs
					qstat -u plumberg.1 | grep plumberg.1 | awk '$(NF-1)=="R"' | awk -F. '{print $1}' > $jobIDsfile
				)
			done
		done
	done
done

# End of file
