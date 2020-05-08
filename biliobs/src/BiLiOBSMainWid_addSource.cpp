#include "BiLiOBSMainWid.h"
#include "BiLiApp.h"
#include "../biliapi/IBiliApi.h"
#include "BiLiPropertyDlg.h"
#include "BiLiMsgDlg.h"
#include "BiLiUserInfoWid.h"
#include "bili_area_cap.h"
#include "bili_obs_source_helper.h"

#include <QMenu>
#include <QCheckBox>
#include<libobs/obs.h>
namespace {
	struct InvokeDoAdjustType
	{
		BiLiOBSMainWid* mainWid;
		obs_sceneitem_t* newSceneItem;
		bool isTimeOut;

		void* operator()()
		{
			obs_scene_t* scene = obs_sceneitem_get_scene(newSceneItem);
			obs_source_t* source = obs_sceneitem_get_source(newSceneItem);

			for (OBSSceneItem& item : OBSEnumSceneItems(scene))
				obs_sceneitem_select(item, false);

			obs_sceneitem_select(newSceneItem, true);

			mainWid->mOnSourceAdded_DoAdjust(newSceneItem, isTimeOut);
			return 0;
		}
	};

	struct OnPropDlgAcceptedTask
	{
		OBSSource newSource;
		OBSSceneItem newSceneItem;
		BiLiOBSMainWid* mainWid;

		void* operator()()
		{
#define CHECK_IF_REMOVED { if (!obs_sceneitem_get_scene(newSceneItem)) { obs_source_release(newSource); obs_sceneitem_release(newSceneItem); return 0; }}(void*)0
			obs_source_t* source = obs_sceneitem_get_source(newSceneItem);
			obs_source_set_enabled(source, true);
			obs_sceneitem_set_visible(newSceneItem, true);

			int countDown = (1000 / 10) * 10; //�ȴ�10�룬�����û���������

			//��async���ͣ��ȵȴ���֡
			if ((obs_source_get_output_flags(newSource) & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO)
			{
				struct obs_source_frame* frame;
				while (countDown-- > 0)
				{
					CHECK_IF_REMOVED;
					frame = obs_source_get_frame(newSource);
					if (frame != nullptr)
					{
						obs_source_release_frame(newSource, frame);
						break;
					}
					os_sleep_ms(10);
				}
			}

			//�ȸ߿���0�Ժ������һ������
			while (countDown-- > 0)
			{
				CHECK_IF_REMOVED;
				if (obs_source_get_width(newSource) != 0 && obs_source_get_height(newSource) != 0)
					break;
				os_sleep_ms(10);
			}

			CHECK_IF_REMOVED;

			//����л�ûִ�е�update
			while (countDown-- > 0)
			{
				CHECK_IF_REMOVED;
				if (!obs_source_has_defer_update(newSource))
					break;
				os_sleep_ms(10);
			}

			//������δ���ŵ����߳�����
			//�����ִ����δ����ʱ���û�ɾ�������sceneitem
			//��ô��߿��ʱ�򣬾��൱�ڵ�����һ��û��scene��sceneitem
			//���ͻ���
			//�ŵ����߳����еĻ����û���û�л��������˲��ȥɾ
			InvokeDoAdjustType doAdjustTask;
			doAdjustTask.isTimeOut = countDown <= 0;
			doAdjustTask.mainWid = mainWid;
			doAdjustTask.newSceneItem = newSceneItem;

			mainWid->mInvokeProcdure(doAdjustTask);

#undef CHECK_IF_REMOVED

			return 0;
		}
	};

	struct OnPropDlgFinishedType
	{
		std::string strSourceId;
		OBSSource sceneSource;
		OBSSource newSource;
		OBSSceneItem newSceneItem;
		BiLiOBSMainWid* This;

		void* operator()(int ret)
		{
			if (ret == QDialog::Accepted) {
				OnPropDlgAcceptedTask task;
				task.mainWid = This;
				task.newSource = newSource;
				task.newSceneItem = newSceneItem;

				This->mPostTask(task);

			}
			else if (ret == QDialog::Rejected)			//rejected
			{
				obs_sceneitem_remove(newSceneItem);
				obs_source_remove(newSource);
			}
			else if (ret == 2)							//gif to media
			{
				obs_sceneitem_remove(newSceneItem);
				obs_source_remove(newSource);
			}

			obs_source_release(newSource);
			obs_source_release(sceneSource); //��obs_scene_release������source���ͷ�

			return 0;
		}
	};
};


void BiLiOBSMainWid::mSltAddSourceActionTriggered(bool checked)
{
	mSltAddSourceButtonClicked();
}

void BiLiOBSMainWid::mSltAddSourceButtonClicked()
{
	QObject* pSender = sender();
	QString strSourceId = pSender->property("source_id").toString();
	if (strSourceId.isEmpty())
	{
		assert(0);
		return;
	}

	struct {
		const char* sourceId;
		QString defaultName;
	} defaultNameTable[] = {
		"dshow_input", tr("Camera"),		//����ͷ
		"game_capture", tr("Game"),			//��Ϸ
		"monitor_capture", tr("Monitor"),	//��ʾ��
		"window_capture", tr("Window"),		//����
		"ffmpeg_source", tr("Media"),		//��ý��
		"text_ft2_source", tr("Text"),		//����
		"image_source", tr("Picture"),		//ͼƬ
	};

	//��ȡĬ����
	int count = sizeof(defaultNameTable) / sizeof(*defaultNameTable);
	int i;
	for (i = 0; i < count; ++i)
	{
		if (strSourceId == defaultNameTable[i].sourceId)
			break;
	}
	if (i >= count)
	{
		assert(0);
		return;
	}

	//��ȡĬ������
	QString name(defaultNameTable[i].defaultName);
	std::list<int> numberList;

	//Ҫƥ���pattern
	std::string pattern = name.toStdString() + "%d%c";

	//��������source��ȡ�������ֵ�
	for (OBSSource& source : OBSEnumSources())
	{
		const char* sourceName = obs_source_get_name(source);
		int num;
		char useless[16];
		if (sscanf(sourceName, pattern.c_str(), &num, useless) == 1)
		{
			numberList.push_back(num);
		}
	}

	//����
	numberList.sort();
	//��������С��
	int nextNum = 1;
	for (int x : numberList)
		if (nextNum == x)
			++nextNum;
		else
			break;

	//�������
	name += lexical_cast<std::string>(nextNum).c_str();
	obs_source_t* sceneSource = obs_get_output_source(0); //�ڲ�������source�����ã�������Ҫ�ͷ�
	if (!sceneSource)
		return;

	obs_scene_t* scene = obs_scene_from_source(sceneSource); //�������scene����sceneSource�����ã����Բ���Ҫ�ͷ�
	if (!scene)
		return;

	obs_data_t* presetParam = NULL;

	//������ԴĬ������
	if (strSourceId == "text_ft2_source")
	{
		presetParam = obs_data_create();
		obs_data_set_string(presetParam, "text", "\xe6\x88\x91\xe4\xbb\x8a\xe5\xa4\xa9\xe6\xb2\xa1\xe5\x90\x83\xe8\x8d\xaf\xef\xbc\x8c\xe6\x84\x9f\xe8\xa7\x89\xe8\x87\xaa\xe5\xb7\xb1\xe8\x90\x8c\xe8\x90\x8c\xe5\x93\x92\xe3\x80\x82");
		obs_data_t* fontObj = obs_data_create();
		obs_data_set_obj(presetParam, "font", fontObj);
		obs_data_set_string(fontObj, "face", "Microsoft YaHei");
		obs_data_set_int(fontObj, "size", 32);
		obs_data_release(fontObj);
	}

	obs_source_t* newSource = obs_source_create(OBS_SOURCE_TYPE_INPUT,
		strSourceId.toUtf8(), name.toUtf8(), presetParam, nullptr);

	if (presetParam)
		obs_data_release(presetParam);

	//obs_load_sources���߼���
	//1����������source
	//2�����غ�obs_add_source��Ȼ���ͷ�
	//3������source��load������ʵ�����ǰ�scene��source������������scene_add
	//4��scene_add����֮���ͷ�source
	//�����ο������Ҳ�����Ƶķ������
	obs_add_source(newSource);
	obs_source_set_enabled(newSource, false);
	obs_sceneitem_t *newSceneItem = obs_scene_add(scene, newSource);
	obs_sceneitem_set_visible(newSceneItem, false);

	BiLiPropertyDialogFactory* dlgFactory = GetBiLiPropertyDialogFactory(strSourceId.toStdString().c_str());
	assert(dlgFactory);
	BiLiPropertyDlg *pDlg = dlgFactory->Create(name, newSceneItem, true, this);
	pDlg->setAttribute(Qt::WA_DeleteOnClose);

	//�������Ȼ���͵��߳���
	obs_source_addref(newSource);
	obs_sceneitem_addref(newSceneItem);

	//�Ի���ر�ʱ�����е�
	OnPropDlgFinishedType onDlgFinished;
	onDlgFinished.newSource = newSource;
	onDlgFinished.newSceneItem = newSceneItem;
	onDlgFinished.sceneSource = sceneSource;
	onDlgFinished.strSourceId = strSourceId.toStdString();
	onDlgFinished.This = this;

	QObject::connect(pDlg, &QDialog::finished, onDlgFinished);

	pDlg->open();
}

void BiLiOBSMainWid::mOnSourceAdded_DoAdjust(obs_sceneitem_t* newSceneItem, bool isTimeOut)
{
	if (obs_sceneitem_get_scene(newSceneItem) == 0)
		return;

	obs_source_t* newSource = obs_sceneitem_get_source(newSceneItem);

	//�����Դ����Ԥ����С���Զ�����
	if (!isTimeOut)
	{
		int baseCX = config_get_uint(mBasicConfig, "Video", "BaseCX");
		int baseCY = config_get_uint(mBasicConfig, "Video", "BaseCY");
		int sourceCX = obs_source_get_width(newSource);
		int sourceCY = obs_source_get_height(newSource);

		if (baseCX < sourceCX || baseCY < sourceCY)
		{
			sceneListWidgetOperator->sceneEditMenuFullScreenSizeImpl(newSceneItem);
		}
	}

	vec2 bound;
	bound.x = obs_source_get_base_width(newSource);
	bound.y = obs_source_get_base_height(newSource);
	obs_sceneitem_set_bounds(newSceneItem, &bound);

	//�����������������ϱ߿򣬵���ʵ�����ݲ�����
	if (!isTimeOut && obs_source_get_id(newSource) == std::string("text_ft2_source"))
	{
		obs_data_t *settings = obs_source_get_settings(newSource);
		bool from_file = obs_data_get_bool(settings, "from_file");
		obs_data_release(settings);
		if (!from_file)
			obs_sceneitem_set_bounds_type(newSceneItem, OBS_BOUNDS_SCALE_INNER);
	}

	obs_source_release(newSource);
	obs_sceneitem_release(newSceneItem);
}

void BiLiOBSMainWid::onScreenShotStateChanged(int state)
{
	BiLiPropertyDlg* pDlg = qobject_cast<BiLiPropertyDlg*>(sender());
	if (state == Qt::Checked){

        on_screen_shot_ = true;

		pDlg->hide();
		showMinimized();

		if (mAreaCap)
			delete mAreaCap;
		mAreaCap = new bili_area_cap(pDlg);
		mAreaCap->setAttribute(Qt::WA_DeleteOnClose);
		mAreaCap->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
		QObject::connect(mAreaCap, &bili_area_cap::mSglSelectComplite, this, &BiLiOBSMainWid::onScreenShotComplete);
		mAreaCap->mShow();
	}
}

void BiLiOBSMainWid::onScreenShotComplete(bool hasSelect)
{
    on_screen_shot_ = false;
	BiLiPropertyDlg* pDlg = mAreaCap->getStartupPropDlg();
	if (!hasSelect){
		QCheckBox *screenShotCB = qVPtr<QCheckBox>::toPtr(pDlg->property("ScreenShotCB"));
		if (screenShotCB)
		{
			screenShotCB->blockSignals(true);
			screenShotCB->setChecked(false);
			screenShotCB->blockSignals(false);
		}
		pDlg->SetIsLimitRectSelected(false);
	}
	else
	{
		pDlg->SetIsLimitRectSelected(true);
	}
	//get............ mAreaCap->mSelectedWidRect;
	showNormal();
	pDlg->show();
	pDlg->mSltOnSettingChanged();
}

void BiLiOBSMainWid::showEvent(QShowEvent *e)
{
    //e->ignore();
    if (on_screen_shot_)
        showMinimized();
}