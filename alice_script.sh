#!/bin/bash

echo "create_user alice password"
sleep 0.5

echo "login alice password"
sleep 0.5

echo "create_group g1"
sleep 1

# Simulate waiting for Bob's join request
sleep 2

echo "accept_request g1 bob"
sleep 1

echo "upload_file hi.pdf g1"
sleep 1
