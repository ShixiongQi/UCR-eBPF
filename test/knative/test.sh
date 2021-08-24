#!/bin/bash
kubectl get svc -n istio-system
curl -HHost:xdp.default.example.com "http://10.111.38.214"