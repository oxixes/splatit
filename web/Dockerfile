# React Router runs in SPA mode here (ssr: false in react-router.config.ts), so
# the build produces a static site under build/client and no server bundle. That
# means `react-router-serve` has nothing to run: serve the files directly.

FROM node:20-alpine AS build
WORKDIR /app

# The glob tolerates package-lock.json being absent, which it is: the lock file
# is deliberately not committed.
COPY package.json package-lock.json* ./

# npm ci refuses to run without a lock file. Resolving from package.json instead
# keeps the build working, and the branch picks ci back up on its own if a lock
# file ever lands in the repository.
RUN if [ -f package-lock.json ]; then npm ci; else npm install --no-audit --no-fund; fi

COPY . .
RUN npm run build

FROM nginx:1.27-alpine
COPY --from=build /app/build/client /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
