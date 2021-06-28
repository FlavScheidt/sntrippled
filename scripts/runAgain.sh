#!/bin/bash

###########################
# STEP 1
#	Rename rippled logs
###########################
cd /var/log/rippled/

#Rename the last log with the timestamp a the begining of the log

LOG_DATE=$(head -1 debug.log | cut -d " " -f1)
LOG_HOUR=$(head -1 debug.log | cut -d " " -f2 | cut -d ":" -f1)
LOG_MIN=$(head -1 debug.log | cut -d " " -f2 | cut -d ":" -f2)
LOG_SEC=$(head -1 debug.log | cut -d " " -f2 | cut -d ":" -f3 | cut -d "." -f1)


mv debug.log "debug_${LOG_DATE}_${LOG_HOUR}_${LOG_MIN}_${LOG_SEC}.log"


###########################
# STEP 2
#	Rename DB
###########################
# cd /var/lib/rippled/

# mkdir "db_${LOG_DATE}_${LOG_HOUR}_${LOG_MIN}_${LOG_SEC}"
# mv db/* "db_${LOG_DATE}_${LOG_HOUR}_${LOG_MIN}_${LOG_SEC}/"


###########################
# STEP 3
#	Rename rename stdout log
###########################
cd /root/sntrippled/my_build/
mv log.out "log_${LOG_DATE}_${LOG_HOUR}_${LOG_MIN}_${LOG_SEC}.out"


###########################
# STEP 4
#	Rename rename grpc logs
###########################
cd /root/sntrippled/grpc

mv log_server.out "logServer_${LOG_DATE}_${LOG_HOUR}_${LOG_MIN}_${LOG_SEC}.out"
mv log_client.out "logClient_${LOG_DATE}_${LOG_HOUR}_${LOG_MIN}_${LOG_SEC}.out"


# # ##########################
# # STEP 5
# # 	Run quorum 1
# # ##########################
# cd /root/sntrippled/my_build/
# ./rippled --conf /opt/local/etc/rippled.cfg --quorum 1 >> log_quorum1.out &

# sleep 10m

# ./rippled  stop --conf /opt/local/etc/rippled.cfg

# ###########################
# # STEP 7
# #	Run grpc
# ###########################
# cd /root/sntrippled/grpc

# node server.js >> log_server.out &

# # /root/sntrippled/my_build/rippled validator_info --conf /opt/local/etc/rippled.cfg | cat | grep "ephemeral_key" | cut -d ":" -f2 | cut -d "\"" -f2 > key.out

# node client.js >> log_client.out &

# sleep 45s

# # ##########################
# # STEP 7
# # 	Run grpc
# # ##########################
# cd /root/sntrippled/grpc

# node server.js >> log_server.out &

# node client.js >> log_client.out &

# sleep 45s

# ###########################
# # STEP 8
# #	Run Rippled again
# ###########################
# cd /root/sntrippled/my_build/

# # ./rippled  stop --conf /opt/local/etc/rippled.cfg 

# # mv log.out log_quorum2.out

# ./rippled --conf /opt/local/etc/rippled.cfg --quorum 1 >> log.out #&
