#pragma once
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace visual_search_test {
struct Server {
  QTcpServer server;
  int chats = 0;
  int descriptions = 0;
  int rankings = 0;
  bool bad_ranking = false;
  QString ranking_evidence;
  int ranking_score = -1;
  QSet<int> insufficient_indices;
  int requests = 0;
  bool cloud = false;
  bool malformed = false;
  bool alternate_field = false;
  bool free_reasoning = false;
  bool redirect = false;
  bool protocol_ok = true;
  int delay = 0;
  QString digest = QStringLiteral("test-digest");
  QString model_name = QStringLiteral("test-vision");
  Server() {
    server.listen(QHostAddress::LocalHost);
    QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
      auto* socket = server.nextPendingConnection();
      auto bytes = std::make_shared<QByteArray>();
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
        bytes->append(socket->readAll());
        const auto boundary = bytes->indexOf("\r\n\r\n");
        if (boundary < 0) return;
        int length = 0;
        for (const auto& line : bytes->left(boundary).split('\n')) {
          if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
        }
        if (bytes->size() < boundary + 4 + length) return;
        ++requests;
        const auto route = bytes->split(' ').value(1);
        const auto body = QJsonDocument::fromJson(bytes->mid(boundary + 4, length)).object();
        QObject::disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);
        QJsonObject response;
        if (route == "/api/tags") {
          response = {{"models", QJsonArray{QJsonObject{{"name", model_name}, {"digest", digest}}}}};
        } else if (route == "/api/show") {
          response = {{"capabilities", QJsonArray{"vision", "completion"}}};
          if (cloud) response.insert("remote_host", "https://cloud.example");
        } else if (route == "/api/chat") {
          ++chats;
          const auto messages = body.value("messages").toArray();
          const auto user = messages.last().toObject();
          const auto images = user.value("images").toArray();
          const auto is_description = body.value("format").toObject().value("properties").toObject().contains("description");
          const auto is_ranking = body.value("format").toObject().value("properties").toObject().contains("scores");
          const auto is_plan = body.value("format").toObject().value("properties").toObject().contains("editInstructions");
          protocol_ok = protocol_ok && images.size() == ((is_ranking || is_plan) ? 0 : 1) && !body.value("stream").toBool(true) &&
              body.value("format").isObject() && body.value("model").toString() == model_name &&
              !user.value("content").toString().contains("secret-filename");
          const auto image = images.isEmpty() ? QImage() : QImage::fromData(QByteArray::fromBase64(images.first().toString().toLatin1()));
          protocol_ok = protocol_ok && (is_ranking || is_plan || (!image.isNull() && image.width() <= 768 && image.height() <= 768));
          const auto red = !image.isNull() && image.pixelColor(0, 0).red() > 180;
          const auto score = QJsonObject{{"score", red ? 90 : 20}, {"reason", red ? "Visible red scene" : "No red subject"}};
          response = {{"done", true}, {"message", QJsonObject{{"content", malformed ? "{}" :
              QString::fromUtf8(QJsonDocument(score).toJson(QJsonDocument::Compact))}}}};
          if (is_description) {
            ++descriptions;
            response.insert("message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"description", red ? "A red scene with no people." : "A blue scene with no people."}}).toJson())}});
          }
          if (is_plan && !malformed) {
            response.insert("message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"query", "red scenes"}, {"editInstructions", "Warm the background and keep people unchanged."}}).toJson())}});
          }
          if (is_ranking) {
            ++rankings;
            const auto input = QJsonDocument::fromJson(user.value("content").toString().toUtf8()).object();
            QJsonArray scores;
            const auto want_blue = input.value("query").toString().contains("blue");
            for (const auto& value : input.value("photos").toArray()) {
              const auto photo = value.toObject();
              const auto match = photo.value("description").toString().contains(want_blue ? "blue" : "red");
              // Reverse response order to prove controller tie order is not model order.
              QJsonObject ranked{{"index", bad_ranking ? 999 : photo.value("index").toInt()},
                  {"score", ranking_score >= 0 ? ranking_score : (match ? 90 : 20)},
                  {"reason", match ? "Matching scene color" : "Different scene color"}};
              const auto fields = body.value("format").toObject().value("properties").toObject()
                  .value("scores").toObject().value("items").toObject().value("properties").toObject();
              if (fields.contains("evidence")) ranked.insert("evidence", ranking_evidence.isEmpty()
                  ? (match ? QStringLiteral("match") : QStringLiteral("contradiction")) : ranking_evidence);
              if (fields.contains("evidence") && insufficient_indices.contains(photo.value("index").toInt()))
                ranked.insert("evidence", "insufficient");
              scores.prepend(ranked);
            }
            response.insert("message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(QJsonObject{{"scores", scores}}).toJson())}});
          }
          if (alternate_field) {
            auto message = response.value("message").toObject();
            message.insert("thinking", (free_reasoning ? QStringLiteral("Let me think. ") : QString()) + message.value("content").toString());
            message.insert("content", "");
            response.insert("message", message);
          }
        }
        const auto payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
        const QByteArray wire = redirect ? QByteArray("HTTP/1.1 302 Found\r\nLocation: http://example.com/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n") :
            QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
            QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;
        QTimer::singleShot(delay, socket, [socket, wire] {
          socket->write(wire);
          socket->disconnectFromHost();
        });
      });
    });
  }
  QVariantMap model() const {
    return {{"backend", "ollama"}, {"vision", true}, {"model", model_name},
            {"endpoint", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())}};
  }
};
}  // namespace visual_search_test
