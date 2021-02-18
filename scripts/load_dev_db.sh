#!/bin/bash -ex


function usage() {
  # Disable the printing on each echo.
  set +x
  echo "Usage:"
  echo "$0 namespace [--load_oss_auth] --help"
  echo ""
  echo "    namespace             The namespace where we want to load the db"
  echo "                              Required."
  echo "    --load_oss_auth       Whether to load the oss auth database or not."
  echo "                              Optional."
  echo ""
  exit 1
}

if [ $# -lt 1 ]; then
  usage
  exit
fi

namespace=""
load_oss_auth=0
repo_path=$(pwd)
versions_file="$(pwd)/src/utils/artifacts/artifact_db_updater/VERSIONS.json"
certs_path=$(pwd)/credentials/certs
while true; do
    if [[ "$1" == "--help" ]]; then
        usage
        exit 1
    elif [[ "$1" == "--load_oss_auth" ]]; then
        load_oss_auth=1
    else
        namespace=$1
    fi
    shift

    if [[ -z "$1" ]]; then
        break
    fi
done


# Port-forward the postgres pod.
postgres_pod=$(kubectl get pod --namespace "$namespace" --selector="name=postgres" \
    --output jsonpath='{.items[0].metadata.name}')
kubectl port-forward pods/"$postgres_pod" 5432:5432 -n "$namespace" &

# Update database with Vizier versions.
bazel run -c opt //src/utils/artifacts/versions_gen:versions_gen -- \
      --repo_path "${repo_path}" --artifact_name vizier --versions_file "${versions_file}"
bazel run -c opt //src/utils/artifacts/artifact_db_updater:artifact_db_updater -- \
    --versions_file "${versions_file}" --postgres_db "pl"

# Update database with CLI versions.
bazel run -c opt //src/utils/artifacts/versions_gen:versions_gen -- \
      --repo_path "${repo_path}" --artifact_name cli --versions_file "${versions_file}"
bazel run -c opt //src/utils/artifacts/artifact_db_updater:artifact_db_updater -- \
    --versions_file "${versions_file}" --postgres_db "pl"

git checkout main "$versions_file"

# Update database with SSL certs.
bazel run -c opt //src/cloud/dnsmgr/load_certs:load_certs -- \
    --certs_path "${certs_path}" --postgres_db "pl"

# Run the kratos and hydra migrate jobs.
if [[ $load_oss_auth -ne 0 ]]; then
    kubectl apply -n "${namespace}" -f "${repo_path}/k8s/cloud/base/kratos/kratos_migrate.yaml"
    kubectl apply -n "${namespace}" -f "${repo_path}/k8s/cloud/base/hydra/hydra_migrate.yaml"
fi

# Kill kubectl port-forward.
kill -15 "$!"
sleep 2
# Make sure process cleans up properly.
kill -9 "$!" 2> /dev/null
