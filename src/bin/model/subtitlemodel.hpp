#ifndef SUBTITLEMODEL_HPP
#define SUBTITLEMODEL_HPP

#include "bin/bin.h"
#include "definitions.h"
#include "gentime.h"
#include "undohelper.hpp"

#include <QAbstractListModel>
#include <QReadWriteLock>

#include <array>
#include <map>
#include <memory>
#include <mlt++/MltProperties.h>
#include <mlt++/Mlt.h>

class DocUndoStack;
class SnapInterface;
class AssetParameterModel;

/* @brief This class is the model for a list of subtitles.
*/

class SubtitleModel:public QAbstractListModel
{
    Q_OBJECT

public:
    /* @brief Construct a subtitle list bound to the timeline */
    explicit SubtitleModel(Mlt::Tractor *tractor = nullptr, QObject *parent = nullptr);

    enum { SubtitleRole = Qt::UserRole + 1, StartPosRole, EndPosRole, StartFrameRole, EndFrameRole };
    /** @brief Function that parses through a subtitle file */ 
    void addSubtitle(GenTime start,GenTime end, QString &str);
    /** @brief Converts string of time to GenTime */ 
    GenTime stringtoTime(QString &str);
    /** @brief Return model data item according to the role passed */ 
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;// overide the same function of QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /** @brief Returns all subtitles in the model */
    QList<SubtitledTime> getAllSubtitles() const;

    /** @brief Registers a snap model to the subtitle model */
    void registerSnap(const std::weak_ptr<SnapInterface> &snapModel);
    
    /** @brief Edit subtitle end timing
        @param startPos is start timing position of subtitles
        @param oldPos is the old position of the end time
        @param pos defines the new position of the end time
    */
    void editEndPos(GenTime startPos, GenTime newEndPos);

    /** @brief Edit subtitle , i.e. text and/or end time
        @param startPos is start timing position of subtitles
        @param newSubtitleText is (new) subtitle text
        @param endPos defines the (new) position of the end time
    */
    void editSubtitle(GenTime startPos, QString newSubtitleText, GenTime endPos);

    /** @brief Remove subtitle at start position (pos) */
    void removeSubtitle(GenTime pos);

    /** @brief Move an existing subtitle
        @param oldPos is original start position of subtitle
        @param newPos is new start position of subtitle
    */
    void moveSubtitle(GenTime oldPos, GenTime newPos);

    /** @brief Exports the subtitle model to json */
    QString toJson();

public slots:
    /** @brief Function that parses through a subtitle file */
    void parseSubtitle();
    
    /** @brief Import model to a temporary subtitle file to which the Subtitle effect is applied*/
    void jsontoSubtitle(const QString &data);

private:
    std::weak_ptr<DocUndoStack> m_undoStack;
    std::map<GenTime, std::pair<QString, GenTime>> m_subtitleList; 

    QString scriptInfoSection="", styleSection = "",eventSection="";
    QString styleName="";
    QString m_subFilePath;

    //To get subtitle file from effects parameter:
    //std::unique_ptr<Mlt::Properties> m_asset;
    //std::shared_ptr<AssetParameterModel> m_model;
    
    std::vector<std::weak_ptr<SnapInterface>> m_regSnaps;
    mutable QReadWriteLock m_lock;
    std::unique_ptr<Mlt::Filter> m_subtitleFilter;
    Mlt::Tractor *m_tractor;

signals:
    void modelChanged();
    
protected:
    /** @brief Helper function that retrieves a pointer to the subtitle model*/
    static std::shared_ptr<SubtitleModel> getModel();
    /** @brief Add start time as snap in the registered snap model */
    void addSnapPoint(GenTime startpos);
    /** @brief Connect changes in model with signal */
    void setup();

};
Q_DECLARE_METATYPE(SubtitleModel *)
#endif // SUBTITLEMODEL_HPP