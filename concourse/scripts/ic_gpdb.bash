#!/bin/bash -l

set -eox pipefail

CWDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source "${CWDIR}/common.bash"

function gen_env(){
  cat > /opt/run_test.sh <<-EOF
		trap look4diffs ERR

		function look4diffs() {

		    diff_files=\`find .. -name regression.diffs\`

		    for diff_file in \${diff_files}; do
			if [ -f "\${diff_file}" ]; then
			    cat <<-FEOF

						======================================================================
						DIFF FILE: \${diff_file}
						----------------------------------------------------------------------

						\$(cat "\${diff_file}")

					FEOF
			fi
		    done
		    exit 1
		}
		source /usr/local/greenplum-db-devel/greenplum_path.sh
		if [ -f /opt/gcc_env.sh ]; then
		    source /opt/gcc_env.sh
		fi
		cd "\${1}/gpdb_src"
		source gpAux/gpdemo/gpdemo-env.sh

		# enable metrics_collector
		gpperfmon_install --enable --password changeme --port 6000
		gpconfig -c gp_enable_gpperfmon -v off
		gpstop -arf
		psql -U gpmon -c "create extension metrics_collector;select pg_sleep(5);" gpperfmon
		ps aux | grep bgworker | grep "metrics collector" | grep -v grep

		#verify metrics_collector is working
		sleep 5
		pushd \$MASTER_DATA_DIRECTORY/pg_log
		export AAA=\`grep -Ri "Metrics Collector access spill data" . | wc -l\`
		if [ \$AAA -gt 0 ]; then echo "good metrics_collector"; else echo "bad metrics_collector"; exit 1; fi
		popd

		make -s ${MAKE_TEST_COMMAND}

		#verify no problem log
		pushd \$MASTER_DATA_DIRECTORY/pg_log
		export BBB=\`grep -Ri "num_active" . | wc -l\`
		if [ \$BBB -gt 0 ]; then echo "bad num_active"; exit 1; else echo "no num_active, good"; fi
		popd
	EOF

	chmod a+x /opt/run_test.sh
}

function setup_gpadmin_user() {
    ./gpdb_src/concourse/scripts/setup_gpadmin_user.bash "$TEST_OS"
}

function _main() {
    if [ -z "${MAKE_TEST_COMMAND}" ]; then
        echo "FATAL: MAKE_TEST_COMMAND is not set"
        exit 1
    fi

    if [ -z "$TEST_OS" ]; then
        echo "FATAL: TEST_OS is not set"
        exit 1
    fi

    case "${TEST_OS}" in
    centos|ubuntu|sles) ;; #Valid
    *)
      echo "FATAL: TEST_OS is set to an invalid value: $TEST_OS"
      echo "Configure TEST_OS to be centos, or ubuntu"
      exit 1
      ;;
    esac

    time install_and_configure_gpdb
    time setup_gpadmin_user
    time make_cluster
    time gen_env
    time run_test

    if [ "${TEST_BINARY_SWAP}" == "true" ]; then
        time ./gpdb_src/concourse/scripts/test_binary_swap_gpdb.bash
    fi

    if [ "${DUMP_DB}" == "true" ]; then
        chmod 777 sqldump
        su gpadmin -c ./gpdb_src/concourse/scripts/dumpdb.bash
    fi
}

_main "$@"
