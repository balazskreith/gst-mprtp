if [ -z "$1" ] 
then
  DST="10.0.0.6"
else 
  DST=$1
fi

if [ -z "$2" ] 
then
  PORT=12345
else 
  PORT=$2
fi

if [ -z "$3" ] 
then
  WAIT=5
else 
  WAIT=$3
fi
echo "#########################################################"
echo "iperf transfer to $DST at port $PORT. Initial Wait: $WAIT"
echo "#########################################################"

#Lambda determines the mean value, 0.1 means 10s
let "mean=10"
sleep $WAIT

for (( c=1; c<=($RANDOM % 10) + 10; c++ ))
do
  let "uni_rand=($RANDOM%32768)"
  exp_rand=$(echo "-1*$mean*l(1-$uni_rand/32768)" | bc -l)
  idle_time=$(python -c "from math import ceil; print ceil($exp_rand)")
  let "bytes_to_transmit=($RANDOM%1000)*600+900000"
  iperf -c $DST -p $PORT -n $bytes_to_transmit &
  sleep $idle_time
done
