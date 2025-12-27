FROM almalinux:8 AS build
ARG arch

# Install dependencies.
RUN dnf install --assumeyes dnf-plugins-core && \
    dnf config-manager --set-enabled powertools && \
    dnf module enable --assumeyes nodejs:24 && \
    dnf install --assumeyes cmake gcc-toolset-15 nasm ninja-build nodejs

# Copy context to container.
WORKDIR /root/
COPY ./ ./cobble/

# Build addon.
WORKDIR /root/cobble/
RUN source /opt/rh/gcc-toolset-15/enable && \
    npm install --global corepack@latest && \
    corepack enable && \
    corepack install && \
    pnpm install --frozen-lockfile && \
    pnpm prepack && \
    strip ./cobble-linux-$arch.node

# Copy addon to blank stage for export.
FROM scratch
ARG arch
COPY --from=build /root/cobble/cobble-linux-$arch.node /
