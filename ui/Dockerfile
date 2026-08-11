FROM node:22-alpine AS web
WORKDIR /web
COPY web/package.json web/package-lock.json* ./
RUN npm install
COPY web/ ./
RUN npm run build

FROM python:3.12-slim
WORKDIR /app
COPY api/requirements.txt api/requirements.txt
RUN pip install --no-cache-dir -r api/requirements.txt
COPY api/ api/
COPY --from=web /web/dist web/dist

ENV CDR_CONFIG=/app/config.toml
ENV UI_HOST=0.0.0.0
EXPOSE 8000
CMD ["python", "api/main.py"]
