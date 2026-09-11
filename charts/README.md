# Fort Kubernetes manifests

Create distinct bearer-token Secrets before applying the Deployment. Keep both
values out of Git: the API token authorizes order-control routes, while the
metrics token authorizes only `/metrics`:

```sh
kubectl create secret generic fort-api-auth \
  --from-literal=api-token='replace-with-a-strong-token'
kubectl create secret generic fort-metrics-auth \
  --from-literal=metrics-token='replace-with-a-different-strong-token'
```

The Deployment also uses persistent audit and WAL claims.  The included
`storage.yaml` is a **pilot-sized example** (20Gi, `ReadWriteOnce`) that uses
the cluster default StorageClass; review the capacity and set
`storageClassName` to a class with the retention/reclaim policy required by
your environment before applying it.  The PVCs deliberately outlive a
Deployment rollout; do not delete them as part of ordinary application
cleanup.

Apply the engine resources:

```sh
kubectl apply -f charts/storage.yaml
kubectl apply -f charts/deployment.yaml
kubectl apply -f charts/service.yaml
```

Before applying outside a local pilot, replace the `:latest` image reference
in `deployment.yaml` with the immutable image digest (or a release tag) that
your deployment process approved. The manifest intentionally does not invent
a registry version or storage class for an operator.

`/metrics` is bearer-protected. The unauthenticated `/healthz` endpoint is
reserved for the Deployment's liveness and readiness probes. To scrape metrics
from a port-forward, include `Authorization: Bearer <metrics-token>`.

`deploy/kubernetes/prometheus-configmap.yaml` is a configuration fragment for
an existing Prometheus workload or operator. Mount `fort-metrics-auth` into
that workload at `/etc/prometheus/secrets/fort-metrics-auth` so its bearer
credential file matches the ConfigMap; this repository does not create a
second Prometheus deployment.
