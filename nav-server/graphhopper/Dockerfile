FROM eclipse-temurin:21-jre
RUN apt-get update \
 && apt-get install -y --no-install-recommends osmium-tool nodejs curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
ADD https://repo1.maven.org/maven2/com/graphhopper/graphhopper-web/11.1/graphhopper-web-11.1.jar /app/graphhopper-web-11.1.jar
COPY graphhopper/config.yml graphhopper/moto_vn.json /app/
COPY tiles/ /app/tiles/
CMD ["java", "-Xmx2g", "-Ddw.graphhopper.graph.location=/data/graph", "-jar", "graphhopper-web-11.1.jar", "server", "config.yml"]
