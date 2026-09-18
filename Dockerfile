FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

RUN git clone https://github.com/alliedmodders/ambuild.git /opt/ambuild \
	&& cd /opt/ambuild && python3 setup.py install

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
