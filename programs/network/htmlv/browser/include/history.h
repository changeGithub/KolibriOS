struct UrlsHistory {
	byte UrlHistory[6000];	
	void AddUrl();
	void GoBack();
	dword CurrentUrl();
};

UrlsHistory BrowserHistory;

void UrlsHistory::GoBack()
{
	WriteDebug(#UrlHistory);
	//find_symbol(#UrlHistory, '|')
	
	j = strlen(#UrlHistory);
	WHILE(UrlHistory[j] <>'|') && (j > 0) j--;
	IF (j > 0) UrlHistory[j] = 0x00;
	WHILE(UrlHistory[j] <>'|') && (j > 0) {
		copystr(#UrlHistory[j], #URL);
		j--;
	}
	UrlHistory[j] = 0x00;
	
	copystr(#URL, #editURL);
	WB1.ShowPage(#URL);
}

void UrlsHistory::AddUrl()
{
	IF (strlen(#UrlHistory)>6000) copystr(#UrlHistory+5000,#UrlHistory);
	copystr("|", #UrlHistory + strlen(#UrlHistory));
	copystr(#URL, #UrlHistory + strlen(#UrlHistory));
}

dword UrlsHistory::CurrentUrl()
{
	EAX=#UrlHistory + find_symbol(#UrlHistory, '|');
}
