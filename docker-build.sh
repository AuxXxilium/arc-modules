#!/bin/bash
# 태그 형식: ghcr.io/<GitHub 사용자명>/arpl-modules:<태그>
docker build \
  -f Dockerfile \
  -t ghcr.io/petersuh-q3/arpl-modules-multi:7.2 \
  -t ghcr.io/petersuh-q3/arpl-modules-multi:7.2-v1000nk \
  -t ghcr.io/petersuh-q3/arpl-modules-multi:7.2-r1000nk \
  -t ghcr.io/petersuh-q3/arpl-modules-multi:7.2-geminilakenk .

