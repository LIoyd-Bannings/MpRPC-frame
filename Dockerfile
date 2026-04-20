FROM ubuntu:22.04 AS builder

RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    libzmq3-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libzookeeper-mt-dev \
    libmysqlclient-dev \
    libssl-dev \
    libprometheus-cpp-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# 自动处理所有 proto 文件
RUN find . -name "*.pb.cc" -type f -delete && \
    find . -name "*.pb.h" -type f -delete
RUN find . -name "*.proto" -exec sh -c 'cd $(dirname "{}") && protoc -I=. --cpp_out=. $(basename "{}")' \;
RUN mkdir -p src/include && cp src/*.pb.h src/include/ || true

# 单核编译防爆内存
RUN mkdir -p build && cd build && cmake .. && make

RUN touch bin/dummy.conf knowledge.txt && mkdir -p prompts && touch prompts/dummy.txt


FROM ubuntu:22.04
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libzmq3-dev \
    libprotobuf-dev \
    libzookeeper-mt-dev \
    libmysqlclient-dev \
    libssl-dev \
    libprometheus-cpp-dev \
    libcurl4-openssl-dev \
    docker.io \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/bin/agent_gateway /app/
COPY --from=builder /build/bin/agent_provider /app/
COPY --from=builder /build/bin/chat_client /app/
COPY --from=builder /build/bin/*.conf /app/
COPY --from=builder /build/knowledge.txt* /app/
COPY --from=builder /build/prompts/ /app/prompts/

ENV TZ=Asia/Shanghai