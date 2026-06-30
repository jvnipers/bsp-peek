FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

RUN dpkg --add-architecture i386 \
	&& apt-get update \
	&& apt-get install -y --no-install-recommends \
	python3-pip python3-venv git ca-certificates \
	gcc-multilib g++-multilib libstdc++6:i386 \
	&& rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/ambuild-venv \
	&& /opt/ambuild-venv/bin/pip install --upgrade pip \
	&& /opt/ambuild-venv/bin/pip install git+https://github.com/alliedmodders/ambuild.git@master
ENV PATH=/opt/ambuild-venv/bin:$PATH

WORKDIR /work

# These three paths must be supplied by the caller via bind-mount or rebuild.
ENV HL2SDKCSGO=/sdks/hl2sdk-csgo \
	MMSOURCE112=/sdks/metamod-source \
	SOURCEMOD112=/sdks/sourcemod

# Default entry: fresh build into /work/build, output ends up in /work/build/package.
CMD ["bash", "-c", "\
    rm -rf build && mkdir build && cd build && \
    python3 ../configure.py --enable-optimize --targets=x86 && \
    ambuild"]
