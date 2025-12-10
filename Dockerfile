FROM rockylinux/rockylinux:8 AS build

# Install dependencies.
RUN dnf install --assumeyes dnf-plugins-core && \
    dnf config-manager --set-enabled powertools && \
    dnf module enable --assumeyes nodejs:24 && \
    dnf install --assumeyes cmake gcc-toolset-15 nasm ninja-build nodejs

# Copy context to container.
WORKDIR /root/
COPY ./ ./cobble/

# Build addon binary
WORKDIR /root/cobble/
RUN source /opt/rh/gcc-toolset-15/enable && \
    npm install --global corepack@latest && \
    corepack enable && \
    corepack install && \
    pnpm install --frozen-lockfile && \
    pnpm prepack && \
    strip ./cobble-linux-x64.node

# Copy addon binary to blank stage for export.
FROM scratch
ARG node_version
COPY --from=build /root/cobble/cobble-linux-x64.node /
