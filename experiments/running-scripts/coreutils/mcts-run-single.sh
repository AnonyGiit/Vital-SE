
# set parms by command
if [ $# != 4 ];then
    echo "Please set the time and search strategy to the experiment :)"
    echo " time(min) : e.g., 1h = 60, 12h = 720, 24h = 1440"
    echo " search strategy:  e.g., random-path, dfs, bfs"
    exit 1
fi

time=$1
search=$2

# --use-batching-search --batch-instructions=10000 \
# for bc in {base64,basename,cat,chcon,chgrp,chmod,chown,chroot,cksum,comm}
# do
bc=$3
sim_limit=$4
klee --simplify-sym-indices --write-cvcs --write-cov --output-module \
    --exit-on-error-type=Ptr --error-location=memcpy.c:29 \
--max-memory=1000 --disable-inlining --optimize=false --use-forked-solver \
--use-cex-cache --libc=uclibc --posix-runtime \
--external-calls=all --only-output-states-covering-new \
--env-file=test.env --run-in-dir=/tmp/sandbox \
--max-sym-array-size=4096 --max-solver-time=30s --max-time=$time\min \
--watchdog --max-memory-inhibit=false --max-static-fork-pct=1 \
--max-static-solve-pct=1 --max-static-cpfork-pct=1 --switch-type=internal \
--search=$search --simulation-search=dfs --package-name=$bc \
--output-dir="klee-out-$time-min-$search-$bc" \
--write-no-tests \
./$bc.bc -sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
# done
# --write-no-tests \
# --search=$search --simulation-search=dfs --package-name=$bc --enable-simulation=false --simulation-limit=$sim_limit \


