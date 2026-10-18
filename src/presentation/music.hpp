#pragma once
namespace phoenix
{
// Local presentation only: no audio state is replicated to other crew clients.
class ServerMusic
{
public:
	~ServerMusic();
	void update(const Application &app);

private:
	Music stream_{};
	int track_ = 0;
	bool attempted_device_ = false, owns_device_ = false;
	void stop();
};
} // namespace phoenix
