FROM ubuntu:22.04

ARG TZ=America/Los_Angeles
ENV TZ="$TZ"
ENV DEBIAN_FRONTEND=noninteractive

ARG CLAUDE_CODE_VERSION=latest

# Install C++ development tools, build tools, and utilities
RUN apt-get update && apt-get install -y --no-install-recommends \
  # C++ compilation and build tools
  build-essential \
  g++ \
  gcc \
  cmake \
  make \
  gdb \
  # Version control and CLI tools
  git \
  gh \
  # Network debugging and utilities
  curl \
  wget \
  ca-certificates \
  dnsutils \
  iproute2 \
  # Firewall and network control
  iptables \
  ipset \
  aggregate \
  # Shell and productivity
  zsh \
  less \
  fzf \
  man-db \
  # Text editors
  vim \
  nano \
  # JSON processing
  jq \
  # Process management
  procps \
  sudo \
  # Misc utilities
  unzip \
  gnupg2 \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install Node.js (needed for Claude Code CLI)
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
  apt-get install -y nodejs

# Create developer user
ARG USERNAME=developer
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
  && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
  && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
  && chmod 0440 /etc/sudoers.d/$USERNAME

# Persist bash history
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  && mkdir /commandhistory \
  && touch /commandhistory/.bash_history \
  && chown -R $USERNAME /commandhistory

# Set DEVCONTAINER environment variable
ENV DEVCONTAINER=true

# Create workspace and config directories
RUN mkdir -p /workspace /home/$USERNAME/.claude && \
  chown -R $USERNAME:$USERNAME /workspace /home/$USERNAME/.claude

WORKDIR /workspace

# Install git-delta for better diffs
ARG GIT_DELTA_VERSION=0.18.2
RUN ARCH=$(dpkg --print-architecture) && \
  wget "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  dpkg -i "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb"

# Install Claude Code CLI via npm
RUN npm install -g @anthropic-ai/claude-code

# Switch to developer user
USER $USERNAME

# Configure git to use delta
RUN git config --global core.pager "delta" && \
  git config --global interactive.diffFilter "delta --color-only" && \
  git config --global delta.navigate "true" && \
  git config --global merge.conflictstyle "diff3" && \
  git config --global diff.colorMoved "default"
