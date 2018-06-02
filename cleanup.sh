#usage: python3 plot.py <(./cleanup.sh results/bandwidth/vilje4/20180602_1730_betainv) ";"
sed 1d $1 | gcut -d';' -f1 --complement | sed 's/;$//'
